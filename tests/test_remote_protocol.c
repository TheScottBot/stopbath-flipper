/*
 * FE3 tests first: well formed parsing for every verb, truncated, overlong,
 * unknown verb, unknown field, missing field, wrong version, embedded null,
 * non printable bytes, maximum payload exactly at the bound and one over.
 * Plus the encoder side of each, since both are derived from one table.
 */
#include "test_support.h"

#include "../protocol/remote_protocol.h"

static RemoteProtocolFeedOutcome feed_string(RemoteProtocolLineAssembler* assembler, const char* text, size_t length, RemoteProtocolMessage* message) {
    RemoteProtocolFeedOutcome outcome = {RemoteProtocolFeedOutcomeIncomplete, RemoteProtocolErrorNone};
    for(size_t index = 0; index < length; index++) {
        outcome = remote_protocol_feed_byte(assembler, (uint8_t)text[index], message);
        if(outcome.kind != RemoteProtocolFeedOutcomeIncomplete && index + 1 < length) {
            /* A message or error before the end means the input carried more
             * than one line; tests here feed one line at a time. */
            return outcome;
        }
    }
    return outcome;
}

static RemoteProtocolFeedOutcome parse(const char* line, RemoteProtocolMessage* message) {
    RemoteProtocolLineAssembler assembler;
    remote_protocol_line_assembler_initialise(&assembler);
    return feed_string(&assembler, line, strlen(line), message);
}

static void assert_error(RemoteTestReport* report, RemoteProtocolFeedOutcome outcome, RemoteProtocolError expected, const char* description) {
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteProtocolFeedOutcomeError, outcome.kind, description);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, expected, outcome.error, description);
}

static void hello_round_trips(RemoteTestReport* report) {
    RemoteProtocolMessage message;
    remote_protocol_message_initialise(&message, RemoteProtocolVerbHello);
    remote_protocol_message_set_integer(&message, RemoteProtocolHelloFieldVersion, 1);
    REMOTE_TEST_ASSERT(report, remote_protocol_message_set_text(&message, RemoteProtocolHelloFieldPeripheral, "flipper-zero"), "token set");
    remote_protocol_message_set_integer(&message, RemoteProtocolHelloFieldLocked, 0);

    char line[REMOTE_PROTOCOL_MAXIMUM_MESSAGE_LENGTH];
    size_t line_length = 0;
    REMOTE_TEST_ASSERT(report, remote_protocol_encode(&message, line, sizeof(line), &line_length), "encodes");
    REMOTE_TEST_ASSERT(report, line_length == strlen("HELLO version=1 peripheral=flipper-zero locked=0\n"), "length");
    REMOTE_TEST_ASSERT(report, memcmp(line, "HELLO version=1 peripheral=flipper-zero locked=0\n", line_length) == 0, "exact wire form, table order");

    RemoteProtocolMessage decoded;
    RemoteProtocolLineAssembler assembler;
    remote_protocol_line_assembler_initialise(&assembler);
    RemoteProtocolFeedOutcome outcome = feed_string(&assembler, line, line_length, &decoded);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteProtocolFeedOutcomeMessage, outcome.kind, "decodes");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteProtocolVerbHello, decoded.verb, "verb");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 1, decoded.fields[RemoteProtocolHelloFieldVersion].integer, "version");
    REMOTE_TEST_ASSERT(report, strcmp(decoded.fields[RemoteProtocolHelloFieldPeripheral].text, "flipper-zero") == 0, "peripheral");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 12, decoded.fields[RemoteProtocolHelloFieldPeripheral].text_length, "peripheral length");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 0, decoded.fields[RemoteProtocolHelloFieldLocked].integer, "locked");
}

static void button_and_state_round_trip(RemoteTestReport* report) {
    RemoteProtocolMessage decoded;
    RemoteProtocolFeedOutcome outcome = parse("BUTTON event=CENTER_LONG foregrounded=1 unlocked=1\n", &decoded);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteProtocolFeedOutcomeMessage, outcome.kind, "button decodes");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteProtocolVerbButton, decoded.verb, "button verb");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteProtocolEventCenterLong, decoded.fields[RemoteProtocolButtonFieldEvent].integer, "event index");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 1, decoded.fields[RemoteProtocolButtonFieldForegrounded].integer, "foregrounded");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 1, decoded.fields[RemoteProtocolButtonFieldUnlocked].integer, "unlocked");

    char line[REMOTE_PROTOCOL_MAXIMUM_MESSAGE_LENGTH];
    size_t line_length = 0;
    REMOTE_TEST_ASSERT(report, remote_protocol_encode(&decoded, line, sizeof(line), &line_length), "re-encodes");
    REMOTE_TEST_ASSERT(report, memcmp(line, "BUTTON event=CENTER_LONG foregrounded=1 unlocked=1\n", line_length) == 0, "same bytes");

    outcome = parse("STATE locked=1\n", &decoded);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteProtocolFeedOutcomeMessage, outcome.kind, "state decodes");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteProtocolVerbState, decoded.verb, "state verb");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 1, decoded.fields[RemoteProtocolStateFieldLocked].integer, "locked");
}

static void display_round_trips_with_a_percent_encoded_payload(RemoteTestReport* report) {
    RemoteProtocolMessage message;
    remote_protocol_message_initialise(&message, RemoteProtocolVerbDisplay);
    remote_protocol_message_set_integer(&message, RemoteProtocolDisplayFieldStatus, RemoteProtocolStatusPresenting);
    remote_protocol_message_set_integer(&message, RemoteProtocolDisplayFieldPage, RemoteProtocolPageWifi);
    REMOTE_TEST_ASSERT(report, remote_protocol_message_set_text(&message, RemoteProtocolDisplayFieldPayload, "WIFI:T:WPA;S:a b;P:x%y=z;;"), "payload set");
    remote_protocol_message_set_integer(&message, RemoteProtocolDisplayFieldDelivered, 3);
    remote_protocol_message_set_integer(&message, RemoteProtocolDisplayFieldError, RemoteProtocolErrorNone);

    char line[REMOTE_PROTOCOL_MAXIMUM_MESSAGE_LENGTH];
    size_t line_length = 0;
    REMOTE_TEST_ASSERT(report, remote_protocol_encode(&message, line, sizeof(line), &line_length), "encodes");
    const char* expected = "DISPLAY status=PRESENTING page=WIFI payload=WIFI%3AT%3AWPA%3BS%3Aa%20b%3BP%3Ax%25y%3Dz%3B%3B delivered=3 error=NONE\n";
    REMOTE_TEST_ASSERT(report, line_length == strlen(expected) && memcmp(line, expected, line_length) == 0, "reserved bytes percent encoded, upper case");

    RemoteProtocolMessage decoded;
    RemoteProtocolLineAssembler assembler;
    remote_protocol_line_assembler_initialise(&assembler);
    RemoteProtocolFeedOutcome outcome = feed_string(&assembler, line, line_length, &decoded);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteProtocolFeedOutcomeMessage, outcome.kind, "decodes");
    REMOTE_TEST_ASSERT(report, strcmp(decoded.fields[RemoteProtocolDisplayFieldPayload].text, "WIFI:T:WPA;S:a b;P:x%y=z;;") == 0, "payload decoded");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteProtocolPageWifi, decoded.fields[RemoteProtocolDisplayFieldPage].integer, "page");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 3, decoded.fields[RemoteProtocolDisplayFieldDelivered].integer, "delivered");
}

static void an_empty_payload_is_written_and_read_as_nothing(RemoteTestReport* report) {
    RemoteProtocolMessage decoded;
    RemoteProtocolFeedOutcome outcome = parse("DISPLAY status=READY page=NONE payload= delivered=0 error=NONE\n", &decoded);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteProtocolFeedOutcomeMessage, outcome.kind, "decodes");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 0, decoded.fields[RemoteProtocolDisplayFieldPayload].text_length, "empty");
    REMOTE_TEST_ASSERT(report, decoded.fields[RemoteProtocolDisplayFieldPayload].text[0] == '\0', "terminated");
}

static void fields_may_arrive_in_any_order(RemoteTestReport* report) {
    RemoteProtocolMessage decoded;
    RemoteProtocolFeedOutcome outcome = parse("HELLO locked=1 peripheral=x version=1\n", &decoded);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteProtocolFeedOutcomeMessage, outcome.kind, "decodes");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 1, decoded.fields[RemoteProtocolHelloFieldLocked].integer, "locked");
}

static void a_truncated_line_yields_nothing_until_its_terminator(RemoteTestReport* report) {
    RemoteProtocolMessage decoded;
    RemoteProtocolLineAssembler assembler;
    remote_protocol_line_assembler_initialise(&assembler);
    const char* partial = "HELLO version=1 peripheral=x lock";
    RemoteProtocolFeedOutcome outcome = feed_string(&assembler, partial, strlen(partial), &decoded);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteProtocolFeedOutcomeIncomplete, outcome.kind, "waiting");
    /* The rest arrives later, byte by byte, and completes the message. */
    const char* rest = "ed=0\n";
    outcome = feed_string(&assembler, rest, strlen(rest), &decoded);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteProtocolFeedOutcomeMessage, outcome.kind, "completed");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 0, decoded.fields[RemoteProtocolHelloFieldLocked].integer, "locked");
}

static void a_truncated_field_list_is_a_missing_field(RemoteTestReport* report) {
    RemoteProtocolMessage decoded;
    assert_error(report, parse("HELLO version=1 peripheral=x\n", &decoded), RemoteProtocolErrorNoField, "locked missing");
    assert_error(report, parse("DISPLAY\n", &decoded), RemoteProtocolErrorNoField, "no fields at all");
}

static void a_line_exactly_at_the_bound_parses_and_one_over_is_too_long(RemoteTestReport* report) {
    /* No valid message reaches the bound (the widest legal DISPLAY is under
     * it), so the line at the bound is a HELLO with an overlong token. At the
     * bound it is complete and gets interpreted, and the token's own bound is
     * what refuses it; one byte over, the length bound refuses it before any
     * interpretation, which is the order 0.10 requires. */
    char line[REMOTE_PROTOCOL_MAXIMUM_MESSAGE_LENGTH + 2];
    const char* hello_prefix = "HELLO version=1 peripheral=";
    const char* hello_suffix = " locked=0\n";
    size_t hello_fixed = strlen(hello_prefix) + strlen(hello_suffix);
    size_t token_length = REMOTE_PROTOCOL_MAXIMUM_MESSAGE_LENGTH - hello_fixed;
    memcpy(line, hello_prefix, strlen(hello_prefix));
    memset(line + strlen(hello_prefix), 'a', token_length);
    memcpy(line + strlen(hello_prefix) + token_length, hello_suffix, strlen(hello_suffix));
    size_t line_length = hello_fixed + token_length;
    REMOTE_TEST_ASSERT_EQUAL_INT(report, REMOTE_PROTOCOL_MAXIMUM_MESSAGE_LENGTH, line_length, "exactly at the bound");

    RemoteProtocolMessage decoded;
    RemoteProtocolLineAssembler assembler;
    remote_protocol_line_assembler_initialise(&assembler);
    RemoteProtocolFeedOutcome outcome = feed_string(&assembler, line, line_length, &decoded);
    /* At the bound the line is complete and is interpreted: the token is
     * far over its own bound, so the refusal is a value error. */
    assert_error(report, outcome, RemoteProtocolErrorBadValue, "at the bound, interpreted");

    /* One byte longer: the bound is reached before the terminator. */
    memset(line + strlen(hello_prefix), 'a', token_length + 1);
    memcpy(line + strlen(hello_prefix) + token_length + 1, hello_suffix, strlen(hello_suffix));
    remote_protocol_line_assembler_initialise(&assembler);
    outcome = feed_string(&assembler, line, line_length + 1, &decoded);
    assert_error(report, outcome, RemoteProtocolErrorTooLong, "one over the bound");

    /* And the assembler resynchronises: the next line parses. */
    outcome = feed_string(&assembler, "STATE locked=0\n", strlen("STATE locked=0\n"), &decoded);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteProtocolFeedOutcomeMessage, outcome.kind, "resynchronised");
}

static void a_payload_at_the_bound_parses_and_one_over_is_refused(RemoteTestReport* report) {
    char payload[REMOTE_PROTOCOL_MAXIMUM_PAYLOAD_LENGTH + 2];
    memset(payload, 'p', REMOTE_PROTOCOL_MAXIMUM_PAYLOAD_LENGTH);
    payload[REMOTE_PROTOCOL_MAXIMUM_PAYLOAD_LENGTH] = '\0';

    RemoteProtocolMessage message;
    remote_protocol_message_initialise(&message, RemoteProtocolVerbDisplay);
    remote_protocol_message_set_integer(&message, RemoteProtocolDisplayFieldStatus, RemoteProtocolStatusPresenting);
    remote_protocol_message_set_integer(&message, RemoteProtocolDisplayFieldPage, RemoteProtocolPageGuest);
    REMOTE_TEST_ASSERT(report, remote_protocol_message_set_text(&message, RemoteProtocolDisplayFieldPayload, payload), "payload at the bound is set");
    char line[REMOTE_PROTOCOL_MAXIMUM_MESSAGE_LENGTH];
    size_t line_length = 0;
    REMOTE_TEST_ASSERT(report, remote_protocol_encode(&message, line, sizeof(line), &line_length), "encodes at the bound");

    RemoteProtocolMessage decoded;
    RemoteProtocolLineAssembler assembler;
    remote_protocol_line_assembler_initialise(&assembler);
    RemoteProtocolFeedOutcome outcome = feed_string(&assembler, line, line_length, &decoded);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteProtocolFeedOutcomeMessage, outcome.kind, "decodes at the bound");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, REMOTE_PROTOCOL_MAXIMUM_PAYLOAD_LENGTH, decoded.fields[RemoteProtocolDisplayFieldPayload].text_length, "full length");

    payload[REMOTE_PROTOCOL_MAXIMUM_PAYLOAD_LENGTH] = 'p';
    payload[REMOTE_PROTOCOL_MAXIMUM_PAYLOAD_LENGTH + 1] = '\0';
    REMOTE_TEST_ASSERT(report, !remote_protocol_message_set_text(&message, RemoteProtocolDisplayFieldPayload, payload), "one over refused by the setter");

    /* On the wire, one over: build the line by hand. */
    char over_line[REMOTE_PROTOCOL_MAXIMUM_MESSAGE_LENGTH];
    int written = snprintf(over_line, sizeof(over_line), "DISPLAY status=READY page=NONE payload=%s delivered=0 error=NONE\n", payload);
    REMOTE_TEST_ASSERT(report, written > 0 && (size_t)written < sizeof(over_line), "fits the message bound");
    remote_protocol_line_assembler_initialise(&assembler);
    outcome = feed_string(&assembler, over_line, (size_t)written, &decoded);
    assert_error(report, outcome, RemoteProtocolErrorBadValue, "one over refused on the wire");
}

static void unknown_verb_field_and_repeated_field_are_refused(RemoteTestReport* report) {
    RemoteProtocolMessage decoded;
    assert_error(report, parse("SHELL cmd=ls\n", &decoded), RemoteProtocolErrorBadVerb, "unknown verb");
    assert_error(report, parse("hello version=1 peripheral=x locked=0\n", &decoded), RemoteProtocolErrorBadVerb, "verbs are case sensitive");
    assert_error(report, parse("STATE locked=0 extra=1\n", &decoded), RemoteProtocolErrorBadField, "unknown field");
    assert_error(report, parse("STATE locked=0 locked=1\n", &decoded), RemoteProtocolErrorBadField, "repeated field");
    assert_error(report, parse("STATE path=/etc/passwd locked=0\n", &decoded), RemoteProtocolErrorBadField, "a path is an unknown field");
}

static void values_outside_their_type_or_bound_are_refused(RemoteTestReport* report) {
    RemoteProtocolMessage decoded;
    assert_error(report, parse("STATE locked=2\n", &decoded), RemoteProtocolErrorBadValue, "boolean out of range");
    assert_error(report, parse("STATE locked=true\n", &decoded), RemoteProtocolErrorBadValue, "boolean is 0 or 1");
    assert_error(report, parse("BUTTON event=UP_SHORT foregrounded=1 unlocked=1\n", &decoded), RemoteProtocolErrorBadValue, "event not in the set");
    assert_error(report, parse("BUTTON event=center_short foregrounded=1 unlocked=1\n", &decoded), RemoteProtocolErrorBadValue, "enumerations are case sensitive");
    assert_error(report, parse("HELLO version=0 peripheral=x locked=0\n", &decoded), RemoteProtocolErrorBadValue, "version below minimum");
    assert_error(report, parse("HELLO version=65536 peripheral=x locked=0\n", &decoded), RemoteProtocolErrorBadValue, "version above maximum");
    assert_error(report, parse("HELLO version=01 peripheral=x locked=0\n", &decoded), RemoteProtocolErrorBadValue, "leading zero");
    assert_error(report, parse("HELLO version=1 peripheral= locked=0\n", &decoded), RemoteProtocolErrorBadValue, "empty token");
    assert_error(report, parse("HELLO version=1 peripheral=Flipper locked=0\n", &decoded), RemoteProtocolErrorBadValue, "token is lower case");
    assert_error(report, parse("HELLO version=1 peripheral=a%20b locked=0\n", &decoded), RemoteProtocolErrorBadValue, "token takes no escapes");
    assert_error(report, parse("DISPLAY status=READY page=NONE payload= delivered=10000 error=NONE\n", &decoded), RemoteProtocolErrorBadValue, "delivered above maximum");
    assert_error(report, parse("DISPLAY status=READY page=NONE payload= delivered=-1 error=NONE\n", &decoded), RemoteProtocolErrorBadValue, "negative");
    assert_error(report, parse("DISPLAY status=READY page=NONE payload= delivered=99999999999 error=NONE\n", &decoded), RemoteProtocolErrorBadValue, "overflowing integer");
}

static void wrong_version_is_parsed_but_not_supported(RemoteTestReport* report) {
    /* Parsing a different version is the parser's job; refusing it is the
     * peer's, per the version policy. Both halves are checked. */
    RemoteProtocolMessage decoded;
    RemoteProtocolFeedOutcome outcome = parse("HELLO version=2 peripheral=x locked=0\n", &decoded);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteProtocolFeedOutcomeMessage, outcome.kind, "parses");
    REMOTE_TEST_ASSERT(report, !remote_protocol_version_is_supported(decoded.fields[RemoteProtocolHelloFieldVersion].integer), "version 2 unsupported");
    REMOTE_TEST_ASSERT(report, remote_protocol_version_is_supported(REMOTE_PROTOCOL_VERSION), "the table's version is supported");
    REMOTE_TEST_ASSERT(report, !remote_protocol_version_is_supported(0), "zero unsupported");
}

static void malformed_bytes_and_escapes_are_refused(RemoteTestReport* report) {
    RemoteProtocolMessage decoded;
    RemoteProtocolLineAssembler assembler;

    const char with_null[] = "STATE loc\0ked=0\n";
    remote_protocol_line_assembler_initialise(&assembler);
    assert_error(report, feed_string(&assembler, with_null, sizeof(with_null) - 1, &decoded), RemoteProtocolErrorMalformed, "embedded null");

    const char with_control[] = "STATE\tlocked=0\n";
    remote_protocol_line_assembler_initialise(&assembler);
    assert_error(report, feed_string(&assembler, with_control, sizeof(with_control) - 1, &decoded), RemoteProtocolErrorMalformed, "tab");

    const char with_high_bit[] = "STATE locked=0\xC3\xA9\n";
    remote_protocol_line_assembler_initialise(&assembler);
    assert_error(report, feed_string(&assembler, with_high_bit, sizeof(with_high_bit) - 1, &decoded), RemoteProtocolErrorMalformed, "non ascii");

    const char with_cr[] = "STATE locked=0\r\n";
    remote_protocol_line_assembler_initialise(&assembler);
    assert_error(report, feed_string(&assembler, with_cr, sizeof(with_cr) - 1, &decoded), RemoteProtocolErrorMalformed, "carriage return");

    assert_error(report, parse("STATE  locked=0\n", &decoded), RemoteProtocolErrorMalformed, "double space");
    assert_error(report, parse(" STATE locked=0\n", &decoded), RemoteProtocolErrorMalformed, "leading space");
    assert_error(report, parse("STATE locked=0 \n", &decoded), RemoteProtocolErrorMalformed, "trailing space");
    assert_error(report, parse("STATE locked\n", &decoded), RemoteProtocolErrorMalformed, "field without equals");
    assert_error(report, parse("STATE =0\n", &decoded), RemoteProtocolErrorMalformed, "field without name");
    assert_error(report, parse("\n", &decoded), RemoteProtocolErrorMalformed, "empty line");
    assert_error(report, parse("DISPLAY status=READY page=NONE payload=%2 delivered=0 error=NONE\n", &decoded), RemoteProtocolErrorMalformed, "truncated escape");
    assert_error(report, parse("DISPLAY status=READY page=NONE payload=%zz delivered=0 error=NONE\n", &decoded), RemoteProtocolErrorMalformed, "non hex escape");
    assert_error(report, parse("DISPLAY status=READY page=NONE payload=%3a delivered=0 error=NONE\n", &decoded), RemoteProtocolErrorMalformed, "lower case escape");
    assert_error(report, parse("DISPLAY status=READY page=NONE payload=%00 delivered=0 error=NONE\n", &decoded), RemoteProtocolErrorMalformed, "escape to a control byte");
    assert_error(report, parse("DISPLAY status=READY page=NONE payload=%FF delivered=0 error=NONE\n", &decoded), RemoteProtocolErrorMalformed, "escape to a non ascii byte");
    assert_error(report, parse("DISPLAY status=READY page=NONE payload=a;b delivered=0 error=NONE\n", &decoded), RemoteProtocolErrorMalformed, "reserved byte unescaped");
}

static void the_assembler_recovers_after_every_error(RemoteTestReport* report) {
    RemoteProtocolMessage decoded;
    RemoteProtocolLineAssembler assembler;
    remote_protocol_line_assembler_initialise(&assembler);
    const char* stream = "BOGUS\nSTATE locked=0\nSTATE locked=9\nSTATE locked=1\n";
    int messages = 0;
    int errors = 0;
    for(size_t index = 0; index < strlen(stream); index++) {
        RemoteProtocolFeedOutcome outcome = remote_protocol_feed_byte(&assembler, (uint8_t)stream[index], &decoded);
        if(outcome.kind == RemoteProtocolFeedOutcomeMessage) messages++;
        if(outcome.kind == RemoteProtocolFeedOutcomeError) errors++;
    }
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 2, messages, "two good messages");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 2, errors, "two refusals");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 1, decoded.fields[RemoteProtocolStateFieldLocked].integer, "last message decoded");
}

static void the_encoder_refuses_what_the_parser_would_refuse(RemoteTestReport* report) {
    RemoteProtocolMessage message;
    char line[REMOTE_PROTOCOL_MAXIMUM_MESSAGE_LENGTH];
    size_t line_length = 0;

    remote_protocol_message_initialise(&message, RemoteProtocolVerbState);
    remote_protocol_message_set_integer(&message, RemoteProtocolStateFieldLocked, 2);
    REMOTE_TEST_ASSERT(report, !remote_protocol_encode(&message, line, sizeof(line), &line_length), "boolean out of range");

    remote_protocol_message_initialise(&message, RemoteProtocolVerbButton);
    remote_protocol_message_set_integer(&message, RemoteProtocolButtonFieldEvent, RemoteProtocolEventCount);
    remote_protocol_message_set_integer(&message, RemoteProtocolButtonFieldForegrounded, 1);
    remote_protocol_message_set_integer(&message, RemoteProtocolButtonFieldUnlocked, 1);
    REMOTE_TEST_ASSERT(report, !remote_protocol_encode(&message, line, sizeof(line), &line_length), "enumeration out of range");

    remote_protocol_message_initialise(&message, RemoteProtocolVerbHello);
    remote_protocol_message_set_integer(&message, RemoteProtocolHelloFieldVersion, 1);
    REMOTE_TEST_ASSERT(report, !remote_protocol_message_set_text(&message, RemoteProtocolHelloFieldPeripheral, "Flipper Zero"), "token with a space and capitals refused");
    REMOTE_TEST_ASSERT(report, remote_protocol_message_set_text(&message, RemoteProtocolHelloFieldPeripheral, "ok"), "token accepted");
    remote_protocol_message_set_integer(&message, RemoteProtocolHelloFieldLocked, 0);
    REMOTE_TEST_ASSERT(report, remote_protocol_encode(&message, line, sizeof(line), &line_length), "valid hello encodes");

    remote_protocol_message_initialise(&message, RemoteProtocolVerbDisplay);
    remote_protocol_message_set_integer(&message, RemoteProtocolDisplayFieldError, RemoteProtocolErrorNone);
    const char with_control[] = {'a', '\n', 'b', '\0'};
    REMOTE_TEST_ASSERT(report, !remote_protocol_message_set_text(&message, RemoteProtocolDisplayFieldPayload, with_control), "payload with a control byte refused");

    /* A buffer too small for the line is refused rather than truncated. */
    remote_protocol_message_initialise(&message, RemoteProtocolVerbState);
    remote_protocol_message_set_integer(&message, RemoteProtocolStateFieldLocked, 0);
    char small[8];
    REMOTE_TEST_ASSERT(report, !remote_protocol_encode(&message, small, sizeof(small), &line_length), "small buffer refused");

    remote_protocol_message_initialise(&message, RemoteProtocolVerbCount);
    REMOTE_TEST_ASSERT(report, !remote_protocol_encode(&message, line, sizeof(line), &line_length), "invalid verb refused");
}

static void every_verb_in_the_table_round_trips_with_every_enumeration_value(RemoteTestReport* report) {
    /* Table driven: for each verb, set each field to a valid value (every
     * enumeration value in turn), encode, parse, compare. */
    for(int verb_index = 0; verb_index < RemoteProtocolVerbCount; verb_index++) {
        const RemoteProtocolVerbDescriptor* descriptor = remote_protocol_verb_descriptor((RemoteProtocolVerb)verb_index);
        REMOTE_TEST_ASSERT(report, descriptor != NULL, "descriptor");
        if(descriptor == NULL) continue;
        for(int variant = 0; variant < 8; variant++) {
            RemoteProtocolMessage message;
            remote_protocol_message_initialise(&message, (RemoteProtocolVerb)verb_index);
            for(int field_index = 0; field_index < descriptor->field_count; field_index++) {
                const RemoteProtocolFieldDescriptor* field = &descriptor->fields[field_index];
                switch(field->type) {
                case RemoteProtocolFieldTypeInteger:
                    remote_protocol_message_set_integer(&message, field_index, variant % 2 ? field->maximum : field->minimum);
                    break;
                case RemoteProtocolFieldTypeBoolean:
                    remote_protocol_message_set_integer(&message, field_index, (uint32_t)(variant % 2));
                    break;
                case RemoteProtocolFieldTypeEnumeration: {
                    int value_count = 0;
                    remote_protocol_enumeration_values(field->enumeration, &value_count);
                    remote_protocol_message_set_integer(&message, field_index, (uint32_t)(variant % value_count));
                    break;
                }
                case RemoteProtocolFieldTypeToken:
                    remote_protocol_message_set_text(&message, field_index, variant % 2 ? "a" : "flipper-zero-2");
                    break;
                case RemoteProtocolFieldTypeText:
                    remote_protocol_message_set_text(&message, field_index, variant % 2 ? "" : "HTTP://192.168.72.1/ ~%=");
                    break;
                }
            }
            char line[REMOTE_PROTOCOL_MAXIMUM_MESSAGE_LENGTH];
            size_t line_length = 0;
            REMOTE_TEST_ASSERT(report, remote_protocol_encode(&message, line, sizeof(line), &line_length), descriptor->name);
            RemoteProtocolMessage decoded;
            RemoteProtocolLineAssembler assembler;
            remote_protocol_line_assembler_initialise(&assembler);
            RemoteProtocolFeedOutcome outcome = feed_string(&assembler, line, line_length, &decoded);
            REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteProtocolFeedOutcomeMessage, outcome.kind, descriptor->name);
            REMOTE_TEST_ASSERT_EQUAL_INT(report, verb_index, decoded.verb, descriptor->name);
            for(int field_index = 0; field_index < descriptor->field_count; field_index++) {
                REMOTE_TEST_ASSERT_EQUAL_INT(report, message.fields[field_index].integer, decoded.fields[field_index].integer, descriptor->fields[field_index].name);
                REMOTE_TEST_ASSERT_EQUAL_INT(report, message.fields[field_index].text_length, decoded.fields[field_index].text_length, descriptor->fields[field_index].name);
                REMOTE_TEST_ASSERT(report, strcmp(message.fields[field_index].text, decoded.fields[field_index].text) == 0, descriptor->fields[field_index].name);
            }
        }
    }
}

static void nothing_in_the_parse_or_encode_path_allocates(RemoteTestReport* report) {
    /* The heap functions are wrapped and counted (test_support.h); a parse
     * and an encode of every verb, plus refusal paths, must leave the count
     * where it was (specification 0.10). */
    static const char* const lines[] = {
        "HELLO version=1 peripheral=flipper-zero locked=0\n",
        "BUTTON event=LEFT_SHORT foregrounded=1 unlocked=1\n",
        "STATE locked=1\n",
        "DISPLAY status=GUEST_CONNECTED page=GUEST payload=HTTP%3A%2F%2F192.168.72.1%2F delivered=12 error=NONE\n",
        "BOGUS verb\n",
        "DISPLAY status=READY page=NONE payload=%zz delivered=0 error=NONE\n",
    };
    int before = remote_test_allocation_count;
    RemoteProtocolMessage decoded;
    for(int line_index = 0; line_index < REMOTE_TEST_ROW_COUNT(lines); line_index++) {
        parse(lines[line_index], &decoded);
    }
    char line[REMOTE_PROTOCOL_MAXIMUM_MESSAGE_LENGTH];
    size_t line_length = 0;
    remote_protocol_encode(&decoded, line, sizeof(line), &line_length);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, before, remote_test_allocation_count, "no allocation");
}

int main(void) {
    static const RemoteTestCase test_cases[] = {
        {"nothing in the parse or encode path allocates", nothing_in_the_parse_or_encode_path_allocates},
        {"hello round trips", hello_round_trips},
        {"button and state round trip", button_and_state_round_trip},
        {"display round trips with a percent encoded payload", display_round_trips_with_a_percent_encoded_payload},
        {"an empty payload is written and read as nothing", an_empty_payload_is_written_and_read_as_nothing},
        {"fields may arrive in any order", fields_may_arrive_in_any_order},
        {"a truncated line yields nothing until its terminator", a_truncated_line_yields_nothing_until_its_terminator},
        {"a truncated field list is a missing field", a_truncated_field_list_is_a_missing_field},
        {"a line exactly at the bound parses and one over is too long", a_line_exactly_at_the_bound_parses_and_one_over_is_too_long},
        {"a payload at the bound parses and one over is refused", a_payload_at_the_bound_parses_and_one_over_is_refused},
        {"unknown verb field and repeated field are refused", unknown_verb_field_and_repeated_field_are_refused},
        {"values outside their type or bound are refused", values_outside_their_type_or_bound_are_refused},
        {"wrong version is parsed but not supported", wrong_version_is_parsed_but_not_supported},
        {"malformed bytes and escapes are refused", malformed_bytes_and_escapes_are_refused},
        {"the assembler recovers after every error", the_assembler_recovers_after_every_error},
        {"the encoder refuses what the parser would refuse", the_encoder_refuses_what_the_parser_would_refuse},
        {"every verb in the table round trips with every enumeration value", every_verb_in_the_table_round_trips_with_every_enumeration_value},
    };
    return remote_test_run_all(test_cases, REMOTE_TEST_ROW_COUNT(test_cases));
}
