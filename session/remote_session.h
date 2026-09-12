/*
 * The client session: the state machine that sits between the transport, the
 * input model, and the display. Pure logic, no SDK, so the whole of it is
 * tested on a development machine (specification FE4, Done when).
 *
 * What it does, and the rules it enforces:
 *
 * - Handshake (specification 2.10): when the host opens the port it sends
 *   HELLO with its version, token and lock state, and waits. The first
 *   DISPLAY the appliance answers with is the acceptance; a DISPLAY carrying
 *   BAD_VERSION is a rejection and the link is shown incompatible.
 * - State replacement (2.5): it holds no authoritative state. Each DISPLAY
 *   replaces the last one wholly. On a link drop it discards everything and
 *   shows not connected, never stale content.
 * - The guard (2.4): a button event is transmitted only while connected,
 *   foregrounded and unlocked. It is never queued: a press that cannot be
 *   sent now is dropped, so a stale press cannot arrive after a reconnection
 *   and end a later session (2.10, consolidated prohibition 8).
 * - Sensitive state (Part 5): the payload, which may carry the Wi-Fi
 *   passphrase, is cleared as soon as the link drops.
 *
 * It interprets nothing about what a button means; that is the appliance's
 * (specification 2.1). It reports the event and renders what it is sent.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../protocol/remote_protocol.h"
#include "../remote_display/remote_display_layout.h"
#include "../remote_input/remote_input_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/* How many outbound messages the session holds before dropping the next and
 * counting it. This is a Flipper implementation constant, not a protocol bound:
 * it bounds this peripheral's own input path and the appliance never sees or
 * honours it, so at promotion (FD20) it was removed from the protocol definition
 * and lives here instead. A press that would exceed it is dropped, never queued
 * across a disconnection. */
#define REMOTE_SESSION_OUTBOUND_QUEUE_DEPTH 4

typedef enum {
    /* The host has not opened the port: no cable, or the appliance has not
     * opened it. Nothing is sent; the display shows not connected. */
    RemoteSessionLinkDown,
    /* The port is open and HELLO has been sent; awaiting the first DISPLAY. */
    RemoteSessionHandshaking,
    /* A DISPLAY has been received; rendering it. */
    RemoteSessionConnected,
    /* The appliance answered HELLO with BAD_VERSION. */
    RemoteSessionIncompatible,
} RemoteSessionLinkState;

typedef struct {
    RemoteSessionLinkState link_state;
    /* The inbound line assembler, reset whenever the link drops so no partial
     * line survives a reconnection. */
    RemoteProtocolLineAssembler inbound;
    /* The identifier this peripheral sends in HELLO. */
    char peripheral_token[REMOTE_PROTOCOL_MAX_TEXT_LENGTH + 1];
    /* The lock state, the one fact this device owns (2.4). Sent in HELLO and
     * STATE and shown on the display. */
    bool screen_locked;
    /* The last DISPLAY received, held only while connected. */
    RemoteProtocolMessage current_display;
    /* Bytes waiting to be sent by the transport. Bounded; a press that would
     * overflow it is dropped and counted, never queued unboundedly. */
    uint8_t output[REMOTE_PROTOCOL_MAXIMUM_MESSAGE_LENGTH * REMOTE_SESSION_OUTBOUND_QUEUE_DEPTH];
    size_t output_length;
    /* How many whole messages are queued: the session's outbound queue depth is
     * a message count, not a byte count, so it is bounded here. */
    int output_message_count;
    /* Milliseconds spent in the handshake since the last HELLO, so a lost
     * handshake is retried rather than hung on forever. Meaningful only while
     * handshaking; reset on entering and leaving that state. */
    uint32_t handshake_elapsed_milliseconds;
    /* Diagnostics for the view specification 2.11 asks for. */
    uint32_t reconnections;
    uint32_t malformed_received;
    uint32_t version_mismatches;
    uint32_t events_dropped_by_guard;
    uint32_t events_dropped_by_output_full;
    uint32_t handshake_retries;
} RemoteSession;

/* How long to wait for the appliance's DISPLAY acceptance before re-sending
 * HELLO. The DISPLAY is the acceptance (PROTOCOL.md handshake); a HELLO or the
 * acceptance can be lost, and the appliance treats a second HELLO as a restart
 * that resends the record (seam 4.2), so a periodic retry recovers a handshake
 * that would otherwise hang. Well under the inbound rate bound, so it is never
 * mistaken for a flood. */
#define REMOTE_SESSION_HANDSHAKE_RETRY_INTERVAL_MILLISECONDS 2000

/* peripheral_token is copied and must be a valid protocol token
 * (lower case, digits, hyphen, 1 to 32 characters); an invalid one is
 * replaced with a safe default so HELLO can always be sent. */
void remote_session_initialise(RemoteSession* session, const char* peripheral_token);

/* The host opened the port (DTR asserted) or the peripheral restarted: send
 * HELLO and begin the handshake. Discards any prior link state first. */
void remote_session_port_opened(RemoteSession* session);

/* The host closed the port, the cable was pulled, or USB suspended: discard
 * link state, clear the sensitive payload, and drop anything unsent. A
 * button that was encoded but not yet drained is discarded here, which is
 * how a stale press is prevented from crossing a reconnection. */
void remote_session_port_closed(RemoteSession* session);

/* Advances the handshake retry clock by the elapsed time since the last call.
 * While the link is handshaking, re-sends HELLO once the retry interval passes,
 * so a lost handshake recovers on its own instead of hanging in "connecting".
 * A no-op in every other link state. The caller passes its own loop interval;
 * the session keeps no clock of its own, so it stays host testable. */
void remote_session_tick(RemoteSession* session, uint32_t elapsed_milliseconds);

/* Feeds bytes received from the appliance. Complete DISPLAY records replace
 * the current one; a BAD_VERSION answer marks the link incompatible;
 * malformed input is counted. Any other verb from the appliance is ignored
 * and counted as malformed, since only DISPLAY flows this way. */
void remote_session_receive(RemoteSession* session, const uint8_t* bytes, size_t byte_count);

/* A reportable button event (the lock has already been applied by the input
 * model, so a locked screen never produces one). Transmits BUTTON only while
 * connected, foregrounded and unlocked; otherwise drops it and counts why.
 * Returns true if it was transmitted. */
bool remote_session_report_event(RemoteSession* session, RemoteReportableEvent event, bool foregrounded);

/* The screen was locked or unlocked locally. Updates the display and, while
 * connected, transmits STATE so the dashboard can show it (2.4). */
void remote_session_lock_changed(RemoteSession* session, bool locked);

/* Drains bytes the transport should send, clearing them. Returns the count. */
size_t remote_session_take_output(RemoteSession* session, uint8_t* destination, size_t destination_capacity);

/* Fills the display state to render: the appliance's record when connected,
 * a link screen otherwise, with the local lock overlaid. nfc_presenting is
 * left false for the glue to set. */
void remote_session_display(const RemoteSession* session, RemoteDisplayState* display_state);

#ifdef __cplusplus
}
#endif
