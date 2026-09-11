/*
 * FE3: the development peer drives every display state, records every
 * event, answers the handshake per the protocol, and produces each of its
 * misbehaviour modes on request.
 */
#include "test_support.h"

#include "../peer/development_peer_core.h"

static void feed_line(DevelopmentPeerCore* peer, const char* line) {
    development_peer_feed(peer, (const uint8_t*)line, strlen(line));
}

/* Takes the peer's output as a terminated string. */
static size_t take_output(DevelopmentPeerCore* peer, char* destination, size_t capacity) {
    size_t length = development_peer_take_output(peer, (uint8_t*)destination, capacity - 1);
    destination[length] = '\0';
    return length;
}

static void hello_is_answered_with_the_current_record(RemoteTestReport* report) {
    DevelopmentPeerCore peer;
    development_peer_initialise(&peer);
    char output[DEVELOPMENT_PEER_OUTPUT_CAPACITY];
    take_output(&peer, output, sizeof(output));
    REMOTE_TEST_ASSERT(report, output[0] == '\0', "nothing before a hello");

    feed_line(&peer, "HELLO version=1 peripheral=flipper-zero locked=0\n");
    take_output(&peer, output, sizeof(output));
    REMOTE_TEST_ASSERT(report, strcmp(output, "DISPLAY status=READY page=NONE payload= delivered=0 error=NONE\n") == 0, "the idle record answers the handshake");
    REMOTE_TEST_ASSERT(report, peer.handshake_complete, "handshake complete");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 1, peer.hellos_received, "counted");
    REMOTE_TEST_ASSERT(report, strcmp(peer.peer_token, "flipper-zero") == 0, "token remembered for the dashboard");
}

static void an_unsupported_version_is_rejected_with_a_typed_record(RemoteTestReport* report) {
    DevelopmentPeerCore peer;
    development_peer_initialise(&peer);
    feed_line(&peer, "HELLO version=2 peripheral=flipper-zero locked=0\n");
    char output[DEVELOPMENT_PEER_OUTPUT_CAPACITY];
    take_output(&peer, output, sizeof(output));
    REMOTE_TEST_ASSERT(report, strcmp(output, "DISPLAY status=READY page=NONE payload= delivered=0 error=BAD_VERSION\n") == 0, "bad version record");
    REMOTE_TEST_ASSERT(report, !peer.handshake_complete, "not accepted");

    /* The misbehaviour mode does the same to a good version. */
    development_peer_initialise(&peer);
    peer.behaviour.reject_every_version = true;
    feed_line(&peer, "HELLO version=1 peripheral=flipper-zero locked=0\n");
    take_output(&peer, output, sizeof(output));
    REMOTE_TEST_ASSERT(report, strstr(output, "error=BAD_VERSION") != NULL, "rejects on demand");
}

static void events_before_hello_get_no_hello_and_are_not_recorded_as_events(RemoteTestReport* report) {
    DevelopmentPeerCore peer;
    development_peer_initialise(&peer);
    feed_line(&peer, "BUTTON event=CENTER_LONG foregrounded=1 unlocked=1\n");
    char output[DEVELOPMENT_PEER_OUTPUT_CAPACITY];
    take_output(&peer, output, sizeof(output));
    REMOTE_TEST_ASSERT(report, strstr(output, "error=NO_HELLO") != NULL, "no hello record");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 0, peer.buttons_received, "a button before hello is not an event");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 1, peer.log_count, "but it is logged");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, DevelopmentPeerLogEntryRefusal, development_peer_log_entry(&peer, 0)->kind, "as a refusal");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteProtocolErrorNoHello, development_peer_log_entry(&peer, 0)->error, "with the code");
}

static void every_event_is_recorded_after_the_handshake(RemoteTestReport* report) {
    DevelopmentPeerCore peer;
    development_peer_initialise(&peer);
    feed_line(&peer, "HELLO version=1 peripheral=flipper-zero locked=0\n");
    static const char* const events[] = {"CENTER_SHORT", "CENTER_LONG", "LEFT_SHORT", "RIGHT_SHORT", "BACK_SHORT"};
    for(int event_index = 0; event_index < 5; event_index++) {
        char line[128];
        snprintf(line, sizeof(line), "BUTTON event=%s foregrounded=1 unlocked=1\n", events[event_index]);
        feed_line(&peer, line);
    }
    feed_line(&peer, "STATE locked=1\n");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 5, peer.buttons_received, "five buttons");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 1, peer.states_received, "one state");
    REMOTE_TEST_ASSERT(report, peer.peer_locked, "lock state tracked for the dashboard");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 7, peer.log_count, "hello, five buttons, one state");
    const DevelopmentPeerLogEntry* second_button = development_peer_log_entry(&peer, 2);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, DevelopmentPeerLogEntryMessage, second_button->kind, "a message entry");
    REMOTE_TEST_ASSERT(report, strcmp(second_button->line, "BUTTON event=CENTER_LONG foregrounded=1 unlocked=1") == 0, "recorded as received");
}

static void the_operator_can_drive_every_display_state(RemoteTestReport* report) {
    DevelopmentPeerCore peer;
    development_peer_initialise(&peer);
    feed_line(&peer, "HELLO version=1 peripheral=flipper-zero locked=0\n");
    char output[DEVELOPMENT_PEER_OUTPUT_CAPACITY];
    take_output(&peer, output, sizeof(output));

    typedef struct {
        RemoteProtocolStatus status;
        RemoteProtocolPage page;
        const char* payload;
        uint32_t delivered;
        RemoteProtocolError error;
        const char* expected;
    } DrivenState;
    const DrivenState states[] = {
        {RemoteProtocolStatusPresenting, RemoteProtocolPageWifi, "WIFI:T:WPA;S:a;P:b;;", 0, RemoteProtocolErrorNone,
         "DISPLAY status=PRESENTING page=WIFI payload=WIFI%3AT%3AWPA%3BS%3Aa%3BP%3Ab%3B%3B delivered=0 error=NONE\n"},
        {RemoteProtocolStatusGuestConnected, RemoteProtocolPageGuest, "HTTP://192.168.72.1/", 3, RemoteProtocolErrorNone,
         "DISPLAY status=GUEST_CONNECTED page=GUEST payload=HTTP%3A%2F%2F192.168.72.1%2F delivered=3 error=NONE\n"},
        {RemoteProtocolStatusTerminating, RemoteProtocolPageNone, NULL, 3, RemoteProtocolErrorNone,
         "DISPLAY status=TERMINATING page=NONE payload= delivered=3 error=NONE\n"},
        {RemoteProtocolStatusRecoveryRequired, RemoteProtocolPageNone, "", 0, RemoteProtocolErrorNone,
         "DISPLAY status=RECOVERY_REQUIRED page=NONE payload= delivered=0 error=NONE\n"},
        {RemoteProtocolStatusReady, RemoteProtocolPageNone, NULL, 0, RemoteProtocolErrorActive,
         "DISPLAY status=READY page=NONE payload= delivered=0 error=ACTIVE\n"},
    };
    for(int state_index = 0; state_index < REMOTE_TEST_ROW_COUNT(states); state_index++) {
        const DrivenState* state = &states[state_index];
        REMOTE_TEST_ASSERT(report, development_peer_set_display(&peer, state->status, state->page, state->payload, state->delivered, state->error), state->expected);
        take_output(&peer, output, sizeof(output));
        REMOTE_TEST_ASSERT(report, strcmp(output, state->expected) == 0, state->expected);
    }

    /* An unencodable record is refused and nothing is sent. */
    REMOTE_TEST_ASSERT(report, !development_peer_set_display(&peer, RemoteProtocolStatusReady, RemoteProtocolPageNone, NULL, 10000, RemoteProtocolErrorNone), "out of range count refused");
    take_output(&peer, output, sizeof(output));
    REMOTE_TEST_ASSERT(report, output[0] == '\0', "nothing sent");
}

static void reconnection_resends_the_full_current_record(RemoteTestReport* report) {
    DevelopmentPeerCore peer;
    development_peer_initialise(&peer);
    feed_line(&peer, "HELLO version=1 peripheral=flipper-zero locked=0\n");
    development_peer_set_display(&peer, RemoteProtocolStatusGuestConnected, RemoteProtocolPageGuest, "HTTP://192.168.72.1/", 7, RemoteProtocolErrorNone);
    char output[DEVELOPMENT_PEER_OUTPUT_CAPACITY];
    take_output(&peer, output, sizeof(output));

    development_peer_link_dropped(&peer);
    REMOTE_TEST_ASSERT(report, !peer.handshake_complete, "link state discarded");
    feed_line(&peer, "BUTTON event=CENTER_LONG foregrounded=1 unlocked=1\n");
    take_output(&peer, output, sizeof(output));
    REMOTE_TEST_ASSERT(report, strstr(output, "error=NO_HELLO") != NULL, "a button after a drop needs a new hello");

    /* A second HELLO is a restart: full record, not a delta. */
    feed_line(&peer, "HELLO version=1 peripheral=flipper-zero locked=1\n");
    take_output(&peer, output, sizeof(output));
    REMOTE_TEST_ASSERT(report, strcmp(output, "DISPLAY status=GUEST_CONNECTED page=GUEST payload=HTTP%3A%2F%2F192.168.72.1%2F delivered=7 error=NONE\n") == 0, "full current record");
    REMOTE_TEST_ASSERT(report, peer.peer_locked, "lock state from the new hello");
}

static void misbehaviour_modes_produce_what_they_promise(RemoteTestReport* report) {
    DevelopmentPeerCore peer;
    development_peer_initialise(&peer);
    char output[DEVELOPMENT_PEER_OUTPUT_CAPACITY];

    development_peer_send_malformed(&peer);
    size_t length = take_output(&peer, output, sizeof(output));
    REMOTE_TEST_ASSERT(report, length > 0 && output[length - 1] == '\n', "a terminated line");
    RemoteProtocolMessage decoded;
    RemoteProtocolFeedOutcome outcome = remote_protocol_parse_line(output, length - 1, &decoded);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteProtocolFeedOutcomeError, outcome.kind, "the library refuses it");

    development_peer_send_oversized(&peer);
    length = take_output(&peer, output, sizeof(output));
    REMOTE_TEST_ASSERT(report, length > REMOTE_PROTOCOL_MAXIMUM_MESSAGE_LENGTH, "past the message bound");
    REMOTE_TEST_ASSERT(report, output[length - 1] == '\n', "terminated, so the receiver reports TOO_LONG");
    RemoteProtocolLineAssembler assembler;
    remote_protocol_line_assembler_initialise(&assembler);
    RemoteProtocolFeedOutcome last = {RemoteProtocolFeedOutcomeIncomplete, RemoteProtocolErrorNone};
    for(size_t index = 0; index < length; index++) {
        last = remote_protocol_feed_byte(&assembler, (uint8_t)output[index], &decoded);
    }
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteProtocolErrorTooLong, last.error, "too long");

    development_peer_send_partial_record(&peer);
    length = take_output(&peer, output, sizeof(output));
    REMOTE_TEST_ASSERT(report, length > 0 && strncmp(output, "DISPLAY", 7) == 0, "starts like a record");
    REMOTE_TEST_ASSERT(report, memchr(output, '\n', length) == NULL, "no terminator, for the shell to cut the link after");

    peer.behaviour.silent = true;
    feed_line(&peer, "HELLO version=1 peripheral=flipper-zero locked=0\n");
    development_peer_set_display(&peer, RemoteProtocolStatusReady, RemoteProtocolPageNone, NULL, 0, RemoteProtocolErrorNone);
    development_peer_send_malformed(&peer);
    length = take_output(&peer, output, sizeof(output));
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 0, length, "silence is silence");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 1, peer.hellos_received, "but the hello was still counted");
}

static void malformed_input_is_logged_as_a_refusal_and_counted(RemoteTestReport* report) {
    DevelopmentPeerCore peer;
    development_peer_initialise(&peer);
    feed_line(&peer, "HELLO version=1 peripheral=flipper-zero locked=0\n");
    feed_line(&peer, "NONSENSE\n");
    feed_line(&peer, "BUTTON event=NOPE foregrounded=1 unlocked=1\n");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 2, peer.refusals, "two refusals");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteProtocolErrorBadVerb, development_peer_log_entry(&peer, 1)->error, "bad verb");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteProtocolErrorBadValue, development_peer_log_entry(&peer, 2)->error, "bad value");
    char output[DEVELOPMENT_PEER_OUTPUT_CAPACITY];
    take_output(&peer, output, sizeof(output));
    /* The appliance answers a refusal with a record carrying the code
     * (extension 2.1), so the photographer sees why nothing happened. */
    REMOTE_TEST_ASSERT(report, strstr(output, "error=BAD_VERB") != NULL, "bad verb reported");
    REMOTE_TEST_ASSERT(report, strstr(output, "error=BAD_VALUE") != NULL, "bad value reported");
}

static void the_demo_script_answers_buttons_with_records_when_enabled(RemoteTestReport* report) {
    /* A stand-in for the appliance for the Part 7 walk-through, not the
     * interpretation table, which belongs to the extension. */
    DevelopmentPeerCore peer;
    development_peer_initialise(&peer);
    peer.behaviour.demo_script = true;
    feed_line(&peer, "HELLO version=1 peripheral=flipper-zero locked=0\n");
    char output[DEVELOPMENT_PEER_OUTPUT_CAPACITY];
    take_output(&peer, output, sizeof(output));

    feed_line(&peer, "BUTTON event=CENTER_SHORT foregrounded=1 unlocked=1\n");
    take_output(&peer, output, sizeof(output));
    REMOTE_TEST_ASSERT(report, strstr(output, "status=PRESENTING page=WIFI") != NULL, "short centre starts and shows wifi");
    feed_line(&peer, "BUTTON event=RIGHT_SHORT foregrounded=1 unlocked=1\n");
    take_output(&peer, output, sizeof(output));
    REMOTE_TEST_ASSERT(report, strstr(output, "page=GUEST") != NULL, "right shows the gallery");
    feed_line(&peer, "BUTTON event=LEFT_SHORT foregrounded=1 unlocked=1\n");
    take_output(&peer, output, sizeof(output));
    REMOTE_TEST_ASSERT(report, strstr(output, "page=WIFI") != NULL, "left shows wifi");
    feed_line(&peer, "BUTTON event=CENTER_LONG foregrounded=1 unlocked=1\n");
    take_output(&peer, output, sizeof(output));
    REMOTE_TEST_ASSERT(report, strstr(output, "status=READY page=NONE payload= ") != NULL, "long centre ends and clears the payload");

    /* The guard is applied even by the stand-in: a false flag is refused. */
    feed_line(&peer, "BUTTON event=CENTER_SHORT foregrounded=1 unlocked=1\n");
    take_output(&peer, output, sizeof(output));
    feed_line(&peer, "BUTTON event=CENTER_LONG foregrounded=0 unlocked=1\n");
    take_output(&peer, output, sizeof(output));
    REMOTE_TEST_ASSERT(report, strstr(output, "error=GUARD") != NULL, "guard refused");
    REMOTE_TEST_ASSERT(report, strstr(output, "status=PRESENTING") != NULL, "session untouched");

    /* Without the script, buttons are recorded and answered with nothing. */
    development_peer_initialise(&peer);
    feed_line(&peer, "HELLO version=1 peripheral=flipper-zero locked=0\n");
    take_output(&peer, output, sizeof(output));
    feed_line(&peer, "BUTTON event=CENTER_SHORT foregrounded=1 unlocked=1\n");
    size_t length = take_output(&peer, output, sizeof(output));
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 0, length, "no interpretation by default");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 1, peer.buttons_received, "still recorded");
}

static void the_log_is_bounded(RemoteTestReport* report) {
    DevelopmentPeerCore peer;
    development_peer_initialise(&peer);
    feed_line(&peer, "HELLO version=1 peripheral=flipper-zero locked=0\n");
    for(int repeat = 0; repeat < DEVELOPMENT_PEER_LOG_CAPACITY + 10; repeat++) {
        feed_line(&peer, "STATE locked=0\n");
    }
    REMOTE_TEST_ASSERT_EQUAL_INT(report, DEVELOPMENT_PEER_LOG_CAPACITY, peer.log_count, "capped");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, DEVELOPMENT_PEER_LOG_CAPACITY + 10, peer.states_received, "counts keep going");
    REMOTE_TEST_ASSERT(report, development_peer_log_entry(&peer, DEVELOPMENT_PEER_LOG_CAPACITY) == NULL, "out of range is null");
}

int main(void) {
    static const RemoteTestCase test_cases[] = {
        {"hello is answered with the current record", hello_is_answered_with_the_current_record},
        {"an unsupported version is rejected with a typed record", an_unsupported_version_is_rejected_with_a_typed_record},
        {"events before hello get no hello and are not recorded as events", events_before_hello_get_no_hello_and_are_not_recorded_as_events},
        {"every event is recorded after the handshake", every_event_is_recorded_after_the_handshake},
        {"the operator can drive every display state", the_operator_can_drive_every_display_state},
        {"reconnection resends the full current record", reconnection_resends_the_full_current_record},
        {"misbehaviour modes produce what they promise", misbehaviour_modes_produce_what_they_promise},
        {"malformed input is logged as a refusal and counted", malformed_input_is_logged_as_a_refusal_and_counted},
        {"the demo script answers buttons with records when enabled", the_demo_script_answers_buttons_with_records_when_enabled},
        {"the log is bounded", the_log_is_bounded},
    };
    return remote_test_run_all(test_cases, REMOTE_TEST_ROW_COUNT(test_cases));
}
