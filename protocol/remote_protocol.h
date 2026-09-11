/*
 * The peripheral protocol: parser and encoder, both driven by the tables
 * generated from protocol.json, so the grammar exists in one place
 * (specification 0.7). Freestanding: no SDK, no allocation, no I/O. Every
 * buffer is sized at compile time from the table's bounds.
 *
 * Every byte that arrives is hostile input (specification 0.10). The parser
 * is a byte at a time state machine that cannot advance past its buffer, and
 * every refusal is a typed error code from the table's own error set so the
 * peer can report it (specification 2.11).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "remote_protocol_tables.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One field's value. Integers, booleans and enumerations live in integer
 * (an enumeration as its index in the generated enum); tokens and text live
 * in text, always terminated, with the decoded length alongside so an
 * embedded byte cannot shorten it silently. */
typedef struct {
    uint32_t integer;
    char text[REMOTE_PROTOCOL_MAX_TEXT_LENGTH + 1];
    size_t text_length;
} RemoteProtocolFieldValue;

/* A decoded message. fields are indexed by the verb's generated field
 * indexes, for example fields[RemoteProtocolDisplayFieldPayload]. */
typedef struct {
    RemoteProtocolVerb verb;
    RemoteProtocolFieldValue fields[REMOTE_PROTOCOL_MAX_FIELDS_PER_VERB];
} RemoteProtocolMessage;

/* Sets the verb and zeroes every field. */
void remote_protocol_message_initialise(RemoteProtocolMessage* message, RemoteProtocolVerb verb);

/* Convenience setters that keep the length in step with the text. Text is
 * copied up to the table's maximum for the field; longer input is refused
 * with false and the field left empty, never truncated. */
bool remote_protocol_message_set_text(RemoteProtocolMessage* message, int field_index, const char* text);
void remote_protocol_message_set_integer(RemoteProtocolMessage* message, int field_index, uint32_t value);

/*
 * Encodes a message as one line, terminator included, into the caller's
 * buffer. Validates every field against the table first: a value outside
 * its type or bound, a text with a byte outside printable ASCII, or a line
 * that would exceed the message bound is refused with false and nothing is
 * written. The encoder writes fields in table order.
 */
bool remote_protocol_encode(const RemoteProtocolMessage* message, char* line, size_t line_capacity, size_t* line_length);

typedef enum {
    /* More bytes are needed before anything can be said. */
    RemoteProtocolFeedOutcomeIncomplete,
    /* A message was decoded into the caller's structure. */
    RemoteProtocolFeedOutcomeMessage,
    /* A line was refused; error says why. Bytes up to the next terminator
     * have already been discarded, so the next byte starts a new line. */
    RemoteProtocolFeedOutcomeError,
} RemoteProtocolFeedOutcomeKind;

typedef struct {
    RemoteProtocolFeedOutcomeKind kind;
    RemoteProtocolError error;
} RemoteProtocolFeedOutcome;

/* Assembles lines from a byte stream. Sized once for the message bound;
 * a line that reaches the bound without a terminator is refused as TOO_LONG
 * when the terminator finally arrives, and everything until then is
 * discarded rather than kept. */
typedef struct {
    char line[REMOTE_PROTOCOL_MAXIMUM_MESSAGE_LENGTH];
    size_t line_length;
    bool overlong;
    bool unprintable;
} RemoteProtocolLineAssembler;

void remote_protocol_line_assembler_initialise(RemoteProtocolLineAssembler* assembler);

/* Feeds one byte. When it completes a line, the line is parsed and the
 * outcome says whether a message resulted. The assembler is ready for the
 * next line on return regardless of the outcome. */
RemoteProtocolFeedOutcome remote_protocol_feed_byte(RemoteProtocolLineAssembler* assembler, uint8_t byte, RemoteProtocolMessage* message);

/* Parses one complete line, without its terminator, in one call. What
 * feed_byte does once it has a line; exposed for tests and the peer. */
RemoteProtocolFeedOutcome remote_protocol_parse_line(const char* line, size_t line_length, RemoteProtocolMessage* message);

/* The version policy: exact match against the supported set, initially just
 * the table's version. */
bool remote_protocol_version_is_supported(uint32_t version);

#ifdef __cplusplus
}
#endif
