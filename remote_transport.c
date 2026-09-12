#include "remote_transport.h"

#include <string.h>

#include <furi.h>
#include <furi_hal_usb.h>
#include <furi_hal_usb_cdc.h>
#include <furi_hal_power.h>
#include <cli/cli_vcp.h>

/* Log tag for the physical link edges, distinct from the application's own tag
 * so the two are told apart in the firmware log (docs/DIAGNOSTICS.md). Only the
 * service loop logs, never the USB callbacks, which run in interrupt context. */
#define TAG "StopBathLink"

/* The link runs on channel 1 (usb_cdc_dual), leaving the firmware command
 * line on channel 0 so the host can still launch the application, read the
 * log, and see free heap while the link runs (evaluation log 4.2). */
#define LINK_CHANNEL 1

/* CDC_DATA_SZ (64) is the USB packet size; sends and reads go a packet at a
 * time. A larger read buffer than the packet is harmless and reduces calls. */
#define TRANSPORT_READ_CHUNK 64

/* How long to wait for a USB transmit to finish before giving up for this
 * pass. furi_hal_cdc_send is asynchronous: it hands the buffer to the USB
 * endpoint and returns, and the buffer must stay valid and unmodified until
 * the tx complete callback fires (usb_uart_bridge.c waits the same way). A
 * host that is not draining the channel makes this time out; the bytes stay
 * queued in the session and are tried again next pass. */
#define TRANSPORT_TX_TIMEOUT_MILLISECONDS 20

struct RemoteTransport {
    RemoteSession* session;
    CliVcp* cli_vcp;
    bool opened;
    /* Whether the session currently believes the port is open, so the
     * service loop only signals on an edge. */
    bool port_open;
    /*
     * Set from the USB callbacks, which run in interrupt context and must not
     * block or take a lock. They only record the latest fact; the main loop
     * reads them in remote_transport_service. usb_present follows the cable
     * (wakeup and suspend); dtr_present follows the host opening the port.
     * volatile because they cross the interrupt to thread boundary; each is a
     * single aligned bool, written in one place and read in another, so no
     * further synchronisation is needed for a last writer wins reading.
     */
    volatile bool usb_present;
    volatile bool dtr_present;
    /* Set by the state callback on a resume, cleared by the service loop once
     * it has re-read the real DTR line from the hardware. A re-enumeration can
     * deliver a resume with no suspend before it (or reorder the two), leaving
     * dtr_present stale true so the port looks open when the host has not opened
     * it yet; re-reading the line on every resume, rather than trusting the
     * cached value, means HELLO is sent only in answer to a real open. */
    volatile bool revalidate_dtr;
    /* Released by the tx complete callback when a transmit finishes, so the
     * send buffer is never reused while the USB hardware is still reading it.
     * Starts available. */
    FuriSemaphore* tx_complete;
    /* The transmit buffer, held on the transport rather than the stack so it
     * outlives a single service call while a transmit is in flight. */
    uint8_t send_buffer[TRANSPORT_READ_CHUNK];
};

static void transport_state_callback(void* context, CdcState state) {
    RemoteTransport* transport = context;
    bool connected = (state == CdcStateConnected);
    transport->usb_present = connected;
    if(connected) {
        /* A resume, possibly a fresh re-enumeration with no suspend before it.
         * The cached DTR may be stale true from before the cable moved, so ask
         * the service loop to re-read the real line before treating the port as
         * open. Without this the port looked open the instant the cable was
         * back and HELLO went out before the host had opened it, which the
         * appliance saw as a HELLO one millisecond after it opened the port. */
        transport->revalidate_dtr = true;
    } else {
        /* On a physical cable pull the host cannot send a DTR drop, so DTR
         * would otherwise stay stale true and, on reinsert, be read as a port
         * still open, starting a handshake with nobody there. Clearing it here
         * means a reconnection waits for the host to open the port again, which
         * the ctrl line callback then reports. */
        transport->dtr_present = false;
        transport->revalidate_dtr = false;
    }
}

static void transport_ctrl_line_callback(void* context, CdcCtrlLine ctrl_lines) {
    RemoteTransport* transport = context;
    /* DTR asserted means the host has the port open; dropped means it closed
     * it. The command line treats DTR the same way (cli_vcp.c at the pinned
     * commit). */
    transport->dtr_present = (ctrl_lines & CdcCtrlLineDTR) != 0;
}

static void transport_rx_callback(void* context) {
    /* Bytes are available. Reading happens in the service loop, so this only
     * exists to satisfy the callback set; nothing to do here without taking
     * a lock, which an interrupt must not. */
    UNUSED(context);
}

static void transport_tx_complete_callback(void* context) {
    RemoteTransport* transport = context;
    /* Runs in USB interrupt context: only release the semaphore, no work. */
    furi_semaphore_release(transport->tx_complete);
}

static void transport_config_callback(void* context, struct usb_cdc_line_coding* config) {
    /* The line coding (baud and framing) is meaningless for a USB CDC
     * channel and is accepted and ignored. */
    UNUSED(context);
    UNUSED(config);
}

RemoteTransport* remote_transport_alloc(RemoteSession* session) {
    RemoteTransport* transport = malloc(sizeof(RemoteTransport));
    transport->session = session;
    transport->cli_vcp = NULL;
    transport->opened = false;
    transport->port_open = false;
    transport->usb_present = false;
    transport->dtr_present = false;
    transport->revalidate_dtr = false;
    transport->tx_complete = furi_semaphore_alloc(1, 1);
    return transport;
}

void remote_transport_free(RemoteTransport* transport) {
    furi_semaphore_free(transport->tx_complete);
    free(transport);
}

bool remote_transport_open(RemoteTransport* transport) {
    if(transport->opened) {
        return true;
    }
    transport->cli_vcp = furi_record_open(RECORD_CLI_VCP);

    /* Unlock any prior mode hold, then switch to dual CDC. set_config returns
     * false when the USB mode is locked, which happens during an RPC session
     * or when the desktop PIN lock is set (evaluation log 4.2). The link
     * cannot open then; the caller shows not connected and may retry. */
    furi_hal_usb_unlock();
    if(!furi_hal_usb_set_config(&usb_cdc_dual, NULL)) {
        furi_record_close(RECORD_CLI_VCP);
        transport->cli_vcp = NULL;
        return false;
    }
    /* Keep the command line alive on channel 0. */
    cli_vcp_enable(transport->cli_vcp);

    static const CdcCallbacks callbacks = {
        transport_tx_complete_callback,
        transport_rx_callback,
        transport_state_callback,
        transport_ctrl_line_callback,
        transport_config_callback,
    };
    furi_hal_cdc_set_callbacks(LINK_CHANNEL, (CdcCallbacks*)&callbacks, transport);

    /* Stop drawing charge current from the appliance while the link is up.
     * The appliance shares a tight USB power budget with the guest radio
     * (evaluation log 4.3, FD10), and a charging peripheral both loads that
     * budget and makes an unplug a larger current transient. Reference
     * counted in the firmware, so the matching exit in close is required. */
    furi_hal_power_suppress_charge_enter();

    /* Seed the observed facts from the current line state, so a port already
     * open when the application starts is noticed on the first service. */
    transport->usb_present = true;
    transport->dtr_present = (furi_hal_cdc_get_ctrl_line_state(LINK_CHANNEL) & CdcCtrlLineDTR) != 0;
    transport->opened = true;
    return true;
}

void remote_transport_close(RemoteTransport* transport) {
    if(!transport->opened) {
        return;
    }
    /* If the link thought the port was open, tell the session it is closing,
     * so the sensitive payload is cleared even on an application exit. */
    if(transport->port_open) {
        remote_session_port_closed(transport->session);
        transport->port_open = false;
    }
    furi_hal_power_suppress_charge_exit();
    furi_hal_cdc_set_callbacks(LINK_CHANNEL, NULL, NULL);
    furi_hal_usb_unlock();
    furi_hal_usb_set_config(&usb_cdc_single, NULL);
    cli_vcp_enable(transport->cli_vcp);
    furi_record_close(RECORD_CLI_VCP);
    transport->cli_vcp = NULL;
    transport->opened = false;
}

void remote_transport_service(RemoteTransport* transport) {
    if(!transport->opened) {
        return;
    }

    /* After a resume, replace any stale cached DTR with the real line, so a
     * reinsertion that delivered no suspend cannot leave the port looking open
     * before the host has opened it. The ctrl line callback still catches a
     * later open; this only corrects a value that was carried across the
     * re-enumeration. Thread context, so the hardware read is safe. */
    if(transport->revalidate_dtr) {
        transport->revalidate_dtr = false;
        bool dtr = (furi_hal_cdc_get_ctrl_line_state(LINK_CHANNEL) & CdcCtrlLineDTR) != 0;
        if(dtr != transport->dtr_present) {
            FURI_LOG_I(TAG, "resume: DTR re-read %d (was %d)", dtr, transport->dtr_present);
        }
        transport->dtr_present = dtr;
    }

    /* The port is usable only when the cable is in and the host has opened
     * it. Signal the session on the edge. */
    bool port_open_now = transport->usb_present && transport->dtr_present;
    if(port_open_now != transport->port_open) {
        transport->port_open = port_open_now;
        if(port_open_now) {
            FURI_LOG_I(TAG, "port opened (usb=%d dtr=%d): handshaking", transport->usb_present, transport->dtr_present);
            remote_session_port_opened(transport->session);
        } else {
            FURI_LOG_I(TAG, "port closed (usb=%d dtr=%d): unsent dropped", transport->usb_present, transport->dtr_present);
            remote_session_port_closed(transport->session);
            /* A transmit may have been in flight when the cable was pulled,
             * in which case the tx complete interrupt never fires and the
             * send slot would stay taken forever, wedging every future send.
             * Normalise the semaphore to available, so the next connection
             * can transmit. Runs in thread context, which is safe. */
            furi_semaphore_acquire(transport->tx_complete, 0);
            furi_semaphore_release(transport->tx_complete);
        }
    }

    /* Read whatever arrived and feed the session. furi_hal_cdc_receive is non
     * blocking and returns the count. */
    uint8_t read_buffer[TRANSPORT_READ_CHUNK];
    int32_t received = furi_hal_cdc_receive(LINK_CHANNEL, read_buffer, sizeof(read_buffer));
    while(received > 0) {
        if(transport->port_open) {
            remote_session_receive(transport->session, read_buffer, (size_t)received);
        }
        /* Drain the channel even if the port is not open, so stale bytes do
         * not accumulate; they are simply not delivered to the session. */
        received = furi_hal_cdc_receive(LINK_CHANNEL, read_buffer, sizeof(read_buffer));
    }

    /* Send whatever the session has queued, a packet at a time. The send slot
     * is taken before any bytes are removed from the session, so a transmit
     * that cannot start (the previous one still in flight, or the host not
     * draining) leaves the bytes queued rather than losing them. The buffer
     * is not overwritten until the previous transmit has completed. */
    while(transport->port_open) {
        if(furi_semaphore_acquire(transport->tx_complete, TRANSPORT_TX_TIMEOUT_MILLISECONDS) != FuriStatusOk) {
            break;
        }
        size_t to_send =
            remote_session_take_output(transport->session, transport->send_buffer, sizeof(transport->send_buffer));
        if(to_send == 0) {
            /* Nothing to send: give the slot back and stop. */
            furi_semaphore_release(transport->tx_complete);
            break;
        }
        furi_hal_cdc_send(LINK_CHANNEL, transport->send_buffer, (uint16_t)to_send);
        /* The slot is released by transport_tx_complete_callback when the USB
         * hardware has finished reading send_buffer. */
    }
}

bool remote_transport_port_is_open(const RemoteTransport* transport) {
    return transport->port_open;
}
