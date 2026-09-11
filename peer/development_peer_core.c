#include "development_peer_core.h"

#include <string.h>

/* Payloads the demo script shows. Not wire values and not credentials: the
 * operator replaces them from the shell before a real walk-through. */
#define DEMO_WIFI_PAYLOAD  "WIFI:T:WPA;S:StopBathExample;P:example1;;"
#define DEMO_GUEST_PAYLOAD "HTTP://192.168.72.1/"

/* The oversized misbehaviour goes well past the message bound so the
 * receiver's TOO_LONG path is the one exercised, not a value bound. */
#define OVERSIZED_LINE_LENGTH (REMOTE_PROTOCOL_MAXIMUM_MESSAGE_LENGTH + 100)

static void blank_record(RemoteProtocolMessage* record) {
    remote_protocol_message_initialise(record, RemoteProtocolVerbDisplay);
    remote_protocol_message_set_integer(record, RemoteProtocolDisplayFieldStatus, RemoteProtocolStatusReady);
    remote_protocol_message_set_integer(record, RemoteProtocolDisplayFieldPage, RemoteProtocolPageNone);
    remote_protocol_message_set_text(record, RemoteProtocolDisplayFieldPayload, "");
    remote_protocol_message_set_integer(record, RemoteProtocolDisplayFieldDelivered, 0);
    remote_protocol_message_set_integer(record, RemoteProtocolDisplayFieldError, RemoteProtocolErrorNone);
}

void development_peer_initialise(DevelopmentPeerCore* peer) {
    memset(peer, 0, sizeof(*peer));
    remote_protocol_line_assembler_initialise(&peer->assembler);
    blank_record(&peer->current_display);
    memcpy(peer->behaviour.demo_wifi_payload, DEMO_WIFI_PAYLOAD, sizeof(DEMO_WIFI_PAYLOAD));
    memcpy(peer->behaviour.demo_guest_payload, DEMO_GUEST_PAYLOAD, sizeof(DEMO_GUEST_PAYLOAD));
}

static void append_output(DevelopmentPeerCore* peer, const uint8_t* bytes, size_t byte_count) {
    if(peer->behaviour.silent) {
        return;
    }
    if(peer->output_length + byte_count > DEVELOPMENT_PEER_OUTPUT_CAPACITY) {
        /* The shell drains after every feed, so this means the shell is
         * not keeping up; dropping is safer than corrupting the stream. */
        peer->output_overflowed = true;
        return;
    }
    memcpy(peer->output + peer->output_length, bytes, byte_count);
    peer->output_length += byte_count;
}

static void send_record(DevelopmentPeerCore* peer, const RemoteProtocolMessage* record) {
    uint8_t line[REMOTE_PROTOCOL_MAXIMUM_MESSAGE_LENGTH];
    size_t line_length = 0;
    if(!remote_protocol_encode(record, (char*)line, sizeof(line), &line_length)) {
        return;
    }
    append_output(peer, line, line_length);
}

/* A copy of the given record with the error field set: the one record
 * published in answer to a rejection carries the code, and the next record
 * for any other reason carries NONE (seam document 3.3). */
static void send_record_with_error(DevelopmentPeerCore* peer, const RemoteProtocolMessage* record, RemoteProtocolError error) {
    RemoteProtocolMessage answer = *record;
    remote_protocol_message_set_integer(&answer, RemoteProtocolDisplayFieldError, error);
    send_record(peer, &answer);
}

/* One log entry per arrival: a message that was acted on, or a refusal,
 * which carries the line when the line parsed but was not allowed. */
static void log_arrival(DevelopmentPeerCore* peer, const RemoteProtocolMessage* message, RemoteProtocolError error) {
    if(peer->log_count >= DEVELOPMENT_PEER_LOG_CAPACITY) return;
    DevelopmentPeerLogEntry* entry = &peer->log[peer->log_count++];
    entry->kind = error == RemoteProtocolErrorNone ? DevelopmentPeerLogEntryMessage : DevelopmentPeerLogEntryRefusal;
    entry->verb = message == NULL ? RemoteProtocolVerbCount : message->verb;
    entry->error = error;
    entry->line[0] = '\0';
    size_t line_length = 0;
    if(message != NULL && remote_protocol_encode(message, entry->line, sizeof(entry->line), &line_length) && line_length > 0) {
        entry->line[line_length - 1] = '\0';
    }
}

static void log_refusal(DevelopmentPeerCore* peer, const RemoteProtocolMessage* message, RemoteProtocolError error) {
    peer->refusals++;
    log_arrival(peer, message, error);
}

static void set_current_page(DevelopmentPeerCore* peer, RemoteProtocolPage page) {
    remote_protocol_message_set_integer(&peer->current_display, RemoteProtocolDisplayFieldPage, page);
    const char* payload = page == RemoteProtocolPageWifi ? peer->behaviour.demo_wifi_payload :
                          page == RemoteProtocolPageGuest ? peer->behaviour.demo_guest_payload : "";
    remote_protocol_message_set_text(&peer->current_display, RemoteProtocolDisplayFieldPayload, payload);
}

/*
 * The demo script: a stand-in for the appliance so the Part 7 sequence can
 * be walked through against this peer. It is deliberately the smallest
 * thing that makes the buttons do something visible, and it is NOT the
 * interpretation table of extension 2.1, which is the appliance's to define.
 */
static void apply_demo_script(DevelopmentPeerCore* peer, const RemoteProtocolMessage* button) {
    uint32_t event = button->fields[RemoteProtocolButtonFieldEvent].integer;
    bool foregrounded = button->fields[RemoteProtocolButtonFieldForegrounded].integer == 1;
    bool unlocked = button->fields[RemoteProtocolButtonFieldUnlocked].integer == 1;
    uint32_t status = peer->current_display.fields[RemoteProtocolDisplayFieldStatus].integer;
    bool active = status == RemoteProtocolStatusPresenting || status == RemoteProtocolStatusGuestConnected;

    /* The guard is applied here too, so a stale or locked press is seen to
     * be refused during the walk-through rather than silently honoured. */
    if(!foregrounded || !unlocked) {
        send_record_with_error(peer, &peer->current_display, RemoteProtocolErrorGuard);
        return;
    }

    switch(event) {
    case RemoteProtocolEventCenterShort:
        if(status != RemoteProtocolStatusReady) {
            send_record_with_error(peer, &peer->current_display, RemoteProtocolErrorActive);
            return;
        }
        remote_protocol_message_set_integer(&peer->current_display, RemoteProtocolDisplayFieldStatus, RemoteProtocolStatusPresenting);
        remote_protocol_message_set_integer(&peer->current_display, RemoteProtocolDisplayFieldDelivered, 0);
        set_current_page(peer, RemoteProtocolPageWifi);
        break;
    case RemoteProtocolEventCenterLong:
        if(!active) {
            send_record_with_error(peer, &peer->current_display, RemoteProtocolErrorNoSession);
            return;
        }
        blank_record(&peer->current_display);
        break;
    case RemoteProtocolEventLeftShort:
        if(!active) return;
        set_current_page(peer, RemoteProtocolPageWifi);
        break;
    case RemoteProtocolEventRightShort:
        if(!active) return;
        set_current_page(peer, RemoteProtocolPageGuest);
        break;
    case RemoteProtocolEventBackShort:
    default:
        return;
    }
    send_record(peer, &peer->current_display);
}

static void handle_hello(DevelopmentPeerCore* peer, const RemoteProtocolMessage* hello) {
    peer->hellos_received++;
    peer->peer_version = hello->fields[RemoteProtocolHelloFieldVersion].integer;
    memcpy(peer->peer_token, hello->fields[RemoteProtocolHelloFieldPeripheral].text, sizeof(peer->peer_token));
    peer->peer_locked = hello->fields[RemoteProtocolHelloFieldLocked].integer == 1;

    /* A second HELLO is a restart: link state is discarded and the answer is
     * the full record again (extension 4.2). */
    peer->handshake_complete = false;
    if(peer->behaviour.reject_every_version || !remote_protocol_version_is_supported(peer->peer_version)) {
        RemoteProtocolMessage rejection;
        blank_record(&rejection);
        send_record_with_error(peer, &rejection, RemoteProtocolErrorBadVersion);
        return;
    }
    peer->handshake_complete = true;
    send_record(peer, &peer->current_display);
}

static void handle_message(DevelopmentPeerCore* peer, const RemoteProtocolMessage* message) {
    if(message->verb == RemoteProtocolVerbHello) {
        log_arrival(peer, message, RemoteProtocolErrorNone);
        handle_hello(peer, message);
        return;
    }
    if(!peer->handshake_complete) {
        /* Not a counted event: nothing before HELLO is acted on. Logged as
         * a refusal that carries what arrived. */
        log_refusal(peer, message, RemoteProtocolErrorNoHello);
        RemoteProtocolMessage rejection;
        blank_record(&rejection);
        send_record_with_error(peer, &rejection, RemoteProtocolErrorNoHello);
        return;
    }
    if(message->verb == RemoteProtocolVerbDisplay) {
        /* The peripheral must never send the appliance's verb. */
        log_refusal(peer, message, RemoteProtocolErrorBadVerb);
        send_record_with_error(peer, &peer->current_display, RemoteProtocolErrorBadVerb);
        return;
    }
    log_arrival(peer, message, RemoteProtocolErrorNone);
    switch(message->verb) {
    case RemoteProtocolVerbButton:
        peer->buttons_received++;
        if(peer->behaviour.demo_script) {
            apply_demo_script(peer, message);
        }
        break;
    case RemoteProtocolVerbState:
        peer->states_received++;
        peer->peer_locked = message->fields[RemoteProtocolStateFieldLocked].integer == 1;
        break;
    case RemoteProtocolVerbDisplay:
    case RemoteProtocolVerbHello:
    case RemoteProtocolVerbCount:
        break;
    }
}

void development_peer_feed(DevelopmentPeerCore* peer, const uint8_t* bytes, size_t byte_count) {
    RemoteProtocolMessage message;
    for(size_t index = 0; index < byte_count; index++) {
        RemoteProtocolFeedOutcome outcome = remote_protocol_feed_byte(&peer->assembler, bytes[index], &message);
        if(outcome.kind == RemoteProtocolFeedOutcomeMessage) {
            handle_message(peer, &message);
        } else if(outcome.kind == RemoteProtocolFeedOutcomeError) {
            log_refusal(peer, NULL, outcome.error);
            if(peer->handshake_complete) {
                send_record_with_error(peer, &peer->current_display, outcome.error);
            } else {
                RemoteProtocolMessage rejection;
                blank_record(&rejection);
                send_record_with_error(peer, &rejection, outcome.error);
            }
        }
    }
}

size_t development_peer_take_output(DevelopmentPeerCore* peer, uint8_t* destination, size_t destination_capacity) {
    size_t taken = peer->output_length < destination_capacity ? peer->output_length : destination_capacity;
    memcpy(destination, peer->output, taken);
    memmove(peer->output, peer->output + taken, peer->output_length - taken);
    peer->output_length -= taken;
    return taken;
}

bool development_peer_set_display(
    DevelopmentPeerCore* peer,
    RemoteProtocolStatus status,
    RemoteProtocolPage page,
    const char* payload,
    uint32_t delivered_count,
    RemoteProtocolError error) {
    RemoteProtocolMessage record;
    remote_protocol_message_initialise(&record, RemoteProtocolVerbDisplay);
    remote_protocol_message_set_integer(&record, RemoteProtocolDisplayFieldStatus, status);
    remote_protocol_message_set_integer(&record, RemoteProtocolDisplayFieldPage, page);
    if(!remote_protocol_message_set_text(&record, RemoteProtocolDisplayFieldPayload, payload == NULL ? "" : payload)) {
        return false;
    }
    remote_protocol_message_set_integer(&record, RemoteProtocolDisplayFieldDelivered, delivered_count);
    remote_protocol_message_set_integer(&record, RemoteProtocolDisplayFieldError, error);

    /* Encodable, or refused before it becomes the current record. */
    char line[REMOTE_PROTOCOL_MAXIMUM_MESSAGE_LENGTH];
    size_t line_length = 0;
    if(!remote_protocol_encode(&record, line, sizeof(line), &line_length)) {
        return false;
    }
    /* The error travels on this record only; the retained record carries
     * NONE so the next publication for any other reason clears it. */
    peer->current_display = record;
    remote_protocol_message_set_integer(&peer->current_display, RemoteProtocolDisplayFieldError, RemoteProtocolErrorNone);
    append_output(peer, (const uint8_t*)line, line_length);
    return true;
}

void development_peer_resend_display(DevelopmentPeerCore* peer) {
    send_record(peer, &peer->current_display);
}

void development_peer_send_error(DevelopmentPeerCore* peer, RemoteProtocolError error) {
    send_record_with_error(peer, &peer->current_display, error);
}

void development_peer_send_malformed(DevelopmentPeerCore* peer) {
    static const char malformed[] = "DISPLAY status=READY page=NONE payload=%zz delivered=0 error=NONE\n";
    append_output(peer, (const uint8_t*)malformed, sizeof(malformed) - 1);
}

void development_peer_send_oversized(DevelopmentPeerCore* peer) {
    uint8_t line[OVERSIZED_LINE_LENGTH];
    memset(line, 'a', sizeof(line) - 1);
    line[sizeof(line) - 1] = (uint8_t)REMOTE_PROTOCOL_TERMINATOR;
    append_output(peer, line, sizeof(line));
}

void development_peer_send_partial_record(DevelopmentPeerCore* peer) {
    uint8_t line[REMOTE_PROTOCOL_MAXIMUM_MESSAGE_LENGTH];
    size_t line_length = 0;
    if(!remote_protocol_encode(&peer->current_display, (char*)line, sizeof(line), &line_length)) {
        return;
    }
    append_output(peer, line, line_length / 2);
}

void development_peer_link_dropped(DevelopmentPeerCore* peer) {
    /* What the appliance forgets on detach: the handshake, any half line,
     * and anything not yet sent. The current record and the log survive. */
    peer->handshake_complete = false;
    remote_protocol_line_assembler_initialise(&peer->assembler);
    peer->output_length = 0;
    peer->output_overflowed = false;
}

const DevelopmentPeerLogEntry* development_peer_log_entry(const DevelopmentPeerCore* peer, int index) {
    if(index < 0 || index >= peer->log_count) return NULL;
    return &peer->log[index];
}
