/*
 * The USB CDC transport for the peripheral link (FD2, FE4): the appliance
 * appears to the Flipper as the host that opens a serial port on the second
 * CDC channel, the firmware's own command line staying on the first.
 *
 * This is the SDK facing edge of the link, so it is deliberately thin: it
 * moves bytes between the USB channel and a RemoteSession, and turns the two
 * physical facts it can observe (the cable, via USB suspend and wakeup, and
 * the host opening the port, via the DTR control line) into the session's
 * port opened and port closed events. Every decision about those bytes lives
 * in the session, which is tested on the host; nothing here is.
 *
 * Evidence, all at the pinned firmware commit, recorded in the evaluation log
 * 4.2: the acquire and release sequence follows
 * applications/main/gpio/usb_uart_bridge.c; the channel API is
 * targets/f7/furi_hal/furi_hal_usb_cdc.h; the CLI stays on channel 0 through
 * cli_vcp_enable.
 */
#pragma once

#include <stdbool.h>

#include "session/remote_session.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RemoteTransport RemoteTransport;

/* Allocates the transport around a session the caller owns. Does not touch
 * USB yet. */
RemoteTransport* remote_transport_alloc(RemoteSession* session);

void remote_transport_free(RemoteTransport* transport);

/* Switches USB to dual CDC, keeps the command line on channel 0, and claims
 * channel 1 for the link. Returns false if the USB mode is locked (an RPC
 * session or a desktop PIN), in which case the link cannot open and the
 * caller shows not connected; it may be retried later. */
bool remote_transport_open(RemoteTransport* transport);

/* Restores single CDC and the command line. Safe to call whether or not
 * open succeeded. */
void remote_transport_close(RemoteTransport* transport);

/* Called once per main loop iteration: reflects the cable and DTR into the
 * session's port opened and port closed events, reads any received bytes
 * into the session, and sends any bytes the session has queued. */
void remote_transport_service(RemoteTransport* transport);

/* Whether the host currently has the port open, for a diagnostic line. */
bool remote_transport_port_is_open(const RemoteTransport* transport);

#ifdef __cplusplus
}
#endif
