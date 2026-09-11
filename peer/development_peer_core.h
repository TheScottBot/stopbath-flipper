/*
 * The development peer's core: everything the peer decides, with no I/O, so
 * it can be tested on the host with bytes in and bytes out.
 *
 * The peer is a test double for the appliance (specification 2.9). It speaks
 * the protocol, sends whatever display record the operator asks for, records
 * every message it receives, and misbehaves on request. It does not contain
 * the appliance's interpretation of button events; the small demo script
 * below exists only so the Part 7 prototype sequence can be walked through,
 * and is labelled as such rather than mistaken for extension 2.1.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../protocol/remote_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Everything the peer received, kept as the re-encoded line so a test or
 * the operator sees exactly what arrived, plus refusals with their code. */
#define DEVELOPMENT_PEER_LOG_CAPACITY 64

typedef enum {
    DevelopmentPeerLogEntryMessage,
    DevelopmentPeerLogEntryRefusal,
} DevelopmentPeerLogEntryKind;

typedef struct {
    DevelopmentPeerLogEntryKind kind;
    /* Message: the verb. Refusal: unused. */
    RemoteProtocolVerb verb;
    /* Refusal: the error code. */
    RemoteProtocolError error;
    /* Message: the line as re-encoded by the library, without terminator. */
    char line[REMOTE_PROTOCOL_MAXIMUM_MESSAGE_LENGTH];
} DevelopmentPeerLogEntry;

/* Output the peer wants sent. Sized for a burst: a handshake answer plus a
 * few records plus one oversized misbehaviour. */
#define DEVELOPMENT_PEER_OUTPUT_CAPACITY (REMOTE_PROTOCOL_MAXIMUM_MESSAGE_LENGTH * 4)

typedef struct {
    /* Whether to answer every HELLO as if its version were unsupported. */
    bool reject_every_version;
    /* Whether to send nothing at all, however the peer is driven. */
    bool silent;
    /* Whether the demo script answers button events with new records. */
    bool demo_script;
    /* What the demo script shows on each page. Example values by default;
     * the shell replaces them for a real walk-through. */
    char demo_wifi_payload[REMOTE_PROTOCOL_MAX_TEXT_LENGTH + 1];
    char demo_guest_payload[REMOTE_PROTOCOL_MAX_TEXT_LENGTH + 1];
} DevelopmentPeerBehaviour;

typedef struct {
    RemoteProtocolLineAssembler assembler;
    RemoteProtocolMessage current_display;
    bool handshake_complete;
    uint32_t peer_version;
    char peer_token[REMOTE_PROTOCOL_MAX_TEXT_LENGTH + 1];
    bool peer_locked;
    DevelopmentPeerBehaviour behaviour;
    DevelopmentPeerLogEntry log[DEVELOPMENT_PEER_LOG_CAPACITY];
    int log_count;
    /* Counts kept for the diagnostic surface 2.11 asks of the appliance. */
    uint32_t hellos_received;
    uint32_t buttons_received;
    uint32_t states_received;
    uint32_t refusals;
    uint8_t output[DEVELOPMENT_PEER_OUTPUT_CAPACITY];
    size_t output_length;
    bool output_overflowed;
} DevelopmentPeerCore;

void development_peer_initialise(DevelopmentPeerCore* peer);

/* Feeds bytes received from the peripheral. Every complete line is parsed
 * and answered per the protocol: HELLO gets the current record (or a
 * BAD_VERSION record), BUTTON and STATE before HELLO get a NO_HELLO record,
 * and everything is logged. */
void development_peer_feed(DevelopmentPeerCore* peer, const uint8_t* bytes, size_t byte_count);

/* Takes the bytes the peer wants sent, clearing them. Returns the count. */
size_t development_peer_take_output(DevelopmentPeerCore* peer, uint8_t* destination, size_t destination_capacity);

/* Operator control: set the record the peer shows and send it. The payload
 * may be NULL for empty. Returns false and sends nothing when the record is
 * not encodable (a bad payload, an out of range count). */
bool development_peer_set_display(
    DevelopmentPeerCore* peer,
    RemoteProtocolStatus status,
    RemoteProtocolPage page,
    const char* payload,
    uint32_t delivered_count,
    RemoteProtocolError error);

/* Re-sends the current record, as the appliance does on reconnection. */
void development_peer_resend_display(DevelopmentPeerCore* peer);

/* Sends the current record carrying an error code, without changing the
 * retained record, so the next ordinary publication clears it (seam 3.3). */
void development_peer_send_error(DevelopmentPeerCore* peer, RemoteProtocolError error);

/* Misbehaviour on demand (specification 2.9): a line that is not the
 * protocol, a line past the message bound, and half a record with no
 * terminator for the shell to disconnect after. */
void development_peer_send_malformed(DevelopmentPeerCore* peer);
void development_peer_send_oversized(DevelopmentPeerCore* peer);
void development_peer_send_partial_record(DevelopmentPeerCore* peer);

/* The link dropped: what the appliance forgets on detach. */
void development_peer_link_dropped(DevelopmentPeerCore* peer);

const DevelopmentPeerLogEntry* development_peer_log_entry(const DevelopmentPeerCore* peer, int index);

#ifdef __cplusplus
}
#endif
