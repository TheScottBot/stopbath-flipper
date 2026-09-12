#include "remote_session.h"

#include <stdio.h>
#include <string.h>

static const char* const SAFE_DEFAULT_TOKEN = "peripheral";

static bool token_is_valid(const char* token) {
    size_t length = strlen(token);
    if(length == 0 || length > REMOTE_PROTOCOL_MAXIMUM_PERIPHERAL_TOKEN_LENGTH) return false;
    for(size_t index = 0; index < length; index++) {
        char character = token[index];
        bool ok = (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '-';
        if(!ok) return false;
    }
    return true;
}

static void reset_display_record(RemoteProtocolMessage* record) {
    remote_protocol_message_initialise(record, RemoteProtocolVerbDisplay);
    remote_protocol_message_set_integer(record, RemoteProtocolDisplayFieldStatus, RemoteProtocolStatusReady);
    remote_protocol_message_set_integer(record, RemoteProtocolDisplayFieldPage, RemoteProtocolPageNone);
    remote_protocol_message_set_text(record, RemoteProtocolDisplayFieldPayload, "");
    remote_protocol_message_set_integer(record, RemoteProtocolDisplayFieldDelivered, 0);
    remote_protocol_message_set_integer(record, RemoteProtocolDisplayFieldError, RemoteProtocolErrorNone);
}

void remote_session_initialise(RemoteSession* session, const char* peripheral_token) {
    memset(session, 0, sizeof(*session));
    const char* token = (peripheral_token != NULL && token_is_valid(peripheral_token)) ? peripheral_token : SAFE_DEFAULT_TOKEN;
    snprintf(session->peripheral_token, sizeof(session->peripheral_token), "%s", token);
    session->link_state = RemoteSessionLinkDown;
    reset_display_record(&session->current_display);
    remote_protocol_line_assembler_initialise(&session->inbound);
}

/* Appends an encoded message to the output, or drops it and counts why. A
 * message that does not fit the remaining buffer is dropped whole; nothing
 * is ever written past the buffer. Returns true if it was queued. */
static bool queue_message(RemoteSession* session, const RemoteProtocolMessage* message, uint32_t* drop_counter) {
    uint8_t line[REMOTE_PROTOCOL_MAXIMUM_MESSAGE_LENGTH];
    size_t line_length = 0;
    if(!remote_protocol_encode(message, (char*)line, sizeof(line), &line_length)) {
        return false;
    }
    /* Bounded by the message count first (the protocol's queue depth), then
     * by the byte buffer as a hard backstop. Either full drops the message
     * whole and counts it; nothing is ever half written. */
    if(session->output_message_count >= (int)REMOTE_PROTOCOL_PERIPHERAL_OUTBOUND_QUEUE_DEPTH ||
       session->output_length + line_length > sizeof(session->output)) {
        if(drop_counter != NULL) (*drop_counter)++;
        return false;
    }
    memcpy(session->output + session->output_length, line, line_length);
    session->output_length += line_length;
    session->output_message_count++;
    return true;
}

static void send_hello(RemoteSession* session) {
    RemoteProtocolMessage hello;
    remote_protocol_message_initialise(&hello, RemoteProtocolVerbHello);
    remote_protocol_message_set_integer(&hello, RemoteProtocolHelloFieldVersion, REMOTE_PROTOCOL_VERSION);
    remote_protocol_message_set_text(&hello, RemoteProtocolHelloFieldPeripheral, session->peripheral_token);
    remote_protocol_message_set_integer(&hello, RemoteProtocolHelloFieldLocked, session->screen_locked ? 1 : 0);
    queue_message(session, &hello, NULL);
}

void remote_session_port_opened(RemoteSession* session) {
    /* A fresh handshake every time, discarding any prior link state and any
     * half received line (2.10). The lock state is retained, since it is the
     * device's own and is what HELLO reports. */
    remote_protocol_line_assembler_initialise(&session->inbound);
    session->output_length = 0;
    session->output_message_count = 0;
    reset_display_record(&session->current_display);
    session->link_state = RemoteSessionHandshaking;
    session->handshake_elapsed_milliseconds = 0;
    send_hello(session);
}

void remote_session_tick(RemoteSession* session, uint32_t elapsed_milliseconds) {
    if(session->link_state != RemoteSessionHandshaking) {
        session->handshake_elapsed_milliseconds = 0;
        return;
    }
    session->handshake_elapsed_milliseconds += elapsed_milliseconds;
    if(session->handshake_elapsed_milliseconds < REMOTE_SESSION_HANDSHAKE_RETRY_INTERVAL_MILLISECONDS) {
        return;
    }
    session->handshake_elapsed_milliseconds = 0;
    session->handshake_retries++;
    /* Only a stale HELLO can be queued while handshaking (a button is sent only
     * while connected), so clearing the output leaves exactly one fresh HELLO
     * rather than letting copies pile up if the previous one was slow to drain. */
    session->output_length = 0;
    session->output_message_count = 0;
    send_hello(session);
}

void remote_session_port_closed(RemoteSession* session) {
    if(session->link_state != RemoteSessionLinkDown) {
        session->reconnections++;
    }
    /* Discard link state and clear the sensitive payload (Part 5). Anything
     * not yet drained is dropped, which is what stops a stale press crossing
     * the reconnection (2.10, consolidated prohibition 8). */
    remote_protocol_line_assembler_initialise(&session->inbound);
    session->output_length = 0;
    session->output_message_count = 0;
    reset_display_record(&session->current_display);
    session->link_state = RemoteSessionLinkDown;
}

static void handle_display(RemoteSession* session, const RemoteProtocolMessage* record) {
    uint32_t error = record->fields[RemoteProtocolDisplayFieldError].integer;
    if(session->link_state != RemoteSessionConnected && error == RemoteProtocolErrorBadVersion) {
        /* The appliance will not talk to this version (2.10 step 2). */
        session->version_mismatches++;
        session->link_state = RemoteSessionIncompatible;
        return;
    }
    session->current_display = *record;
    session->link_state = RemoteSessionConnected;
}

void remote_session_receive(RemoteSession* session, const uint8_t* bytes, size_t byte_count) {
    RemoteProtocolMessage message;
    for(size_t index = 0; index < byte_count; index++) {
        RemoteProtocolFeedOutcome outcome = remote_protocol_feed_byte(&session->inbound, bytes[index], &message);
        if(outcome.kind == RemoteProtocolFeedOutcomeMessage) {
            if(message.verb == RemoteProtocolVerbDisplay) {
                handle_display(session, &message);
            } else {
                /* Only DISPLAY flows from the appliance; anything else is a
                 * peer speaking out of turn and is treated as malformed. */
                session->malformed_received++;
            }
        } else if(outcome.kind == RemoteProtocolFeedOutcomeError) {
            session->malformed_received++;
        }
    }
}

bool remote_session_report_event(RemoteSession* session, RemoteReportableEvent event, bool foregrounded) {
    if(session->link_state != RemoteSessionConnected) {
        return false;
    }
    /* The guard (2.4): foregrounded and unlocked, both checked here even
     * though the input model already suppresses events while locked, because
     * a peripheral that self certifies its own guard is trusting the
     * untrusted side, and the same reasoning applies within the peripheral. */
    if(!foregrounded || session->screen_locked) {
        session->events_dropped_by_guard++;
        return false;
    }

    RemoteProtocolEvent wire_event;
    switch(event) {
    case RemoteReportableEventCenterShort:
        wire_event = RemoteProtocolEventCenterShort;
        break;
    case RemoteReportableEventCenterLong:
        wire_event = RemoteProtocolEventCenterLong;
        break;
    case RemoteReportableEventLeftShort:
        wire_event = RemoteProtocolEventLeftShort;
        break;
    case RemoteReportableEventRightShort:
        wire_event = RemoteProtocolEventRightShort;
        break;
    case RemoteReportableEventBackShort:
        wire_event = RemoteProtocolEventBackShort;
        break;
    case RemoteReportableEventCount:
    default:
        return false;
    }

    RemoteProtocolMessage button;
    remote_protocol_message_initialise(&button, RemoteProtocolVerbButton);
    remote_protocol_message_set_integer(&button, RemoteProtocolButtonFieldEvent, wire_event);
    /* Both true by construction: we only reach here foregrounded and
     * unlocked. Sending them is belt and braces for the appliance. */
    remote_protocol_message_set_integer(&button, RemoteProtocolButtonFieldForegrounded, 1);
    remote_protocol_message_set_integer(&button, RemoteProtocolButtonFieldUnlocked, 1);
    return queue_message(session, &button, &session->events_dropped_by_output_full);
}

void remote_session_lock_changed(RemoteSession* session, bool locked) {
    session->screen_locked = locked;
    if(session->link_state != RemoteSessionConnected) {
        /* Reported to the appliance only when there is a link to report on;
         * the display reflects it locally regardless. */
        return;
    }
    RemoteProtocolMessage state;
    remote_protocol_message_initialise(&state, RemoteProtocolVerbState);
    remote_protocol_message_set_integer(&state, RemoteProtocolStateFieldLocked, locked ? 1 : 0);
    queue_message(session, &state, NULL);
}

size_t remote_session_take_output(RemoteSession* session, uint8_t* destination, size_t destination_capacity) {
    size_t taken = session->output_length < destination_capacity ? session->output_length : destination_capacity;
    memcpy(destination, session->output, taken);
    memmove(session->output, session->output + taken, session->output_length - taken);
    session->output_length -= taken;
    /* Recount whole messages remaining by their terminators, so a partial
     * drain leaves the count honest and a fresh message can still be
     * queued up to the depth. */
    session->output_message_count = 0;
    for(size_t index = 0; index < session->output_length; index++) {
        if(session->output[index] == REMOTE_PROTOCOL_TERMINATOR) session->output_message_count++;
    }
    return taken;
}

void remote_session_display(const RemoteSession* session, RemoteDisplayState* display_state) {
    remote_display_state_initialise(display_state);
    display_state->screen_locked = session->screen_locked;

    switch(session->link_state) {
    case RemoteSessionLinkDown:
        display_state->link_connected = false;
        break;
    case RemoteSessionHandshaking:
        display_state->link_connected = false;
        display_state->link_connecting = true;
        break;
    case RemoteSessionIncompatible:
        display_state->link_connected = false;
        display_state->link_incompatible = true;
        break;
    case RemoteSessionConnected: {
        display_state->link_connected = true;
        const RemoteProtocolMessage* record = &session->current_display;
        display_state->status = (int)record->fields[RemoteProtocolDisplayFieldStatus].integer;
        display_state->page = (int)record->fields[RemoteProtocolDisplayFieldPage].integer;
        snprintf(display_state->payload, sizeof(display_state->payload), "%s", record->fields[RemoteProtocolDisplayFieldPayload].text);
        display_state->delivered_count = record->fields[RemoteProtocolDisplayFieldDelivered].integer;
        /* The error field maps to the display's error code text through the
         * protocol enumeration's wire names, so the code shown is the code
         * sent. NONE leaves the band off. */
        uint32_t error = record->fields[RemoteProtocolDisplayFieldError].integer;
        if(error != RemoteProtocolErrorNone) {
            int error_count = 0;
            const char* const* error_names = remote_protocol_enumeration_values(RemoteProtocolEnumerationError, &error_count);
            if((int)error < error_count) {
                snprintf(display_state->error_code, sizeof(display_state->error_code), "%s", error_names[error]);
            }
        }
        break;
    }
    }
}
