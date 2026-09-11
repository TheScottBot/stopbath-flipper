/*
 * FE4 tests first: handshake success, version mismatch rejection, disconnect
 * during a message, reconnect discards local state, button events are not
 * queued across a disconnection, an event is not emitted while the guard is
 * unsatisfied.
 */
#include "test_support.h"

#include "../session/remote_session.h"

/* A stand-in appliance built from the protocol library, so a test drives the
 * session with real bytes rather than hand written lines. */
static size_t encode_display(uint8_t* line, RemoteProtocolStatus status, RemoteProtocolPage page, const char* payload, uint32_t delivered, RemoteProtocolError error) {
    RemoteProtocolMessage record;
    remote_protocol_message_initialise(&record, RemoteProtocolVerbDisplay);
    remote_protocol_message_set_integer(&record, RemoteProtocolDisplayFieldStatus, status);
    remote_protocol_message_set_integer(&record, RemoteProtocolDisplayFieldPage, page);
    remote_protocol_message_set_text(&record, RemoteProtocolDisplayFieldPayload, payload);
    remote_protocol_message_set_integer(&record, RemoteProtocolDisplayFieldDelivered, delivered);
    remote_protocol_message_set_integer(&record, RemoteProtocolDisplayFieldError, error);
    size_t line_length = 0;
    remote_protocol_encode(&record, (char*)line, REMOTE_PROTOCOL_MAXIMUM_MESSAGE_LENGTH, &line_length);
    return line_length;
}

static void receive_display(RemoteSession* session, RemoteProtocolStatus status, RemoteProtocolPage page, const char* payload, uint32_t delivered, RemoteProtocolError error) {
    uint8_t line[REMOTE_PROTOCOL_MAXIMUM_MESSAGE_LENGTH];
    size_t length = encode_display(line, status, page, payload, delivered, error);
    remote_session_receive(session, line, length);
}

/* Takes the session's output as a terminated string. */
static void take_output(RemoteSession* session, char* destination, size_t capacity) {
    size_t length = remote_session_take_output(session, (uint8_t*)destination, capacity - 1);
    destination[length] = '\0';
}

static void a_fresh_session_is_down_and_silent(RemoteTestReport* report) {
    RemoteSession session;
    remote_session_initialise(&session, "flipper-zero");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteSessionLinkDown, session.link_state, "down");
    char output[256];
    take_output(&session, output, sizeof(output));
    REMOTE_TEST_ASSERT(report, output[0] == '\0', "nothing sent");
    RemoteDisplayState display_state;
    remote_session_display(&session, &display_state);
    REMOTE_TEST_ASSERT(report, !display_state.link_connected, "shows not connected");
}

static void opening_the_port_sends_hello_and_begins_handshake(RemoteTestReport* report) {
    RemoteSession session;
    remote_session_initialise(&session, "flipper-zero");
    remote_session_port_opened(&session);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteSessionHandshaking, session.link_state, "handshaking");
    char output[256];
    take_output(&session, output, sizeof(output));
    REMOTE_TEST_ASSERT(report, strcmp(output, "HELLO version=1 peripheral=flipper-zero locked=0\n") == 0, "hello sent with lock state");

    RemoteDisplayState display_state;
    remote_session_display(&session, &display_state);
    REMOTE_TEST_ASSERT(report, !display_state.link_connected && display_state.link_connecting, "connecting");
}

static void hello_carries_the_current_lock_state(RemoteTestReport* report) {
    RemoteSession session;
    remote_session_initialise(&session, "flipper-zero");
    remote_session_lock_changed(&session, true);
    remote_session_port_opened(&session);
    char output[256];
    take_output(&session, output, sizeof(output));
    REMOTE_TEST_ASSERT(report, strcmp(output, "HELLO version=1 peripheral=flipper-zero locked=1\n") == 0, "locked hello");
}

static void the_first_display_completes_the_handshake(RemoteTestReport* report) {
    RemoteSession session;
    remote_session_initialise(&session, "flipper-zero");
    remote_session_port_opened(&session);
    char output[256];
    take_output(&session, output, sizeof(output));
    receive_display(&session, RemoteProtocolStatusPresenting, RemoteProtocolPageWifi, "WIFI:T:WPA;S:a;P:b;;", 2, RemoteProtocolErrorNone);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteSessionConnected, session.link_state, "connected");

    RemoteDisplayState display_state;
    remote_session_display(&session, &display_state);
    REMOTE_TEST_ASSERT(report, display_state.link_connected, "connected display");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteDisplayStatusPresenting, display_state.status, "status rendered");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteDisplayPageWifi, display_state.page, "page rendered");
    REMOTE_TEST_ASSERT(report, strcmp(display_state.payload, "WIFI:T:WPA;S:a;P:b;;") == 0, "payload rendered");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 2, display_state.delivered_count, "count rendered");
}

static void a_bad_version_answer_marks_the_link_incompatible(RemoteTestReport* report) {
    RemoteSession session;
    remote_session_initialise(&session, "flipper-zero");
    remote_session_port_opened(&session);
    char output[256];
    take_output(&session, output, sizeof(output));
    receive_display(&session, RemoteProtocolStatusReady, RemoteProtocolPageNone, "", 0, RemoteProtocolErrorBadVersion);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteSessionIncompatible, session.link_state, "incompatible");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 1, session.version_mismatches, "counted");

    RemoteDisplayState display_state;
    remote_session_display(&session, &display_state);
    REMOTE_TEST_ASSERT(report, display_state.link_incompatible, "incompatible display");

    /* No button is sent while incompatible. */
    REMOTE_TEST_ASSERT(report, !remote_session_report_event(&session, RemoteReportableEventCenterShort, true), "no event while incompatible");
}

static void a_display_replaces_the_previous_one_wholly(RemoteTestReport* report) {
    RemoteSession session;
    remote_session_initialise(&session, "flipper-zero");
    remote_session_port_opened(&session);
    char output[256];
    take_output(&session, output, sizeof(output));
    receive_display(&session, RemoteProtocolStatusGuestConnected, RemoteProtocolPageGuest, "HTTP://192.168.72.1/", 5, RemoteProtocolErrorNone);
    receive_display(&session, RemoteProtocolStatusReady, RemoteProtocolPageNone, "", 0, RemoteProtocolErrorNone);
    RemoteDisplayState display_state;
    remote_session_display(&session, &display_state);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteDisplayStatusReady, display_state.status, "replaced");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 0, display_state.delivered_count, "count replaced, not merged");
    REMOTE_TEST_ASSERT(report, display_state.payload[0] == '\0', "payload cleared by the new record");
}

static void a_disconnect_mid_message_discards_the_partial_line(RemoteTestReport* report) {
    RemoteSession session;
    remote_session_initialise(&session, "flipper-zero");
    remote_session_port_opened(&session);
    char output[256];
    take_output(&session, output, sizeof(output));
    receive_display(&session, RemoteProtocolStatusPresenting, RemoteProtocolPageWifi, "WIFI:T:WPA;S:a;P:b;;", 0, RemoteProtocolErrorNone);

    /* Half a record arrives, then the cable is pulled. */
    const char* half = "DISPLAY status=GUEST_CONNECTED page=GUEST payl";
    remote_session_receive(&session, (const uint8_t*)half, strlen(half));
    remote_session_port_closed(&session);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteSessionLinkDown, session.link_state, "down");

    /* Reconnect: a new HELLO, and the rest of that old line must not be
     * completed against the fresh assembler. */
    remote_session_port_opened(&session);
    take_output(&session, output, sizeof(output));
    const char* rest = "oad=x delivered=0 error=NONE\n";
    remote_session_receive(&session, (const uint8_t*)rest, strlen(rest));
    /* The rest alone is not a valid line, so it is malformed and ignored;
     * the session stays handshaking, not connected on stale bytes. */
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteSessionHandshaking, session.link_state, "still handshaking");
}

static void reconnect_discards_local_state_and_clears_the_payload(RemoteTestReport* report) {
    RemoteSession session;
    remote_session_initialise(&session, "flipper-zero");
    remote_session_port_opened(&session);
    char output[256];
    take_output(&session, output, sizeof(output));
    receive_display(&session, RemoteProtocolStatusPresenting, RemoteProtocolPageWifi, "WIFI:T:WPA;S:secret;P:passphrase;;", 0, RemoteProtocolErrorNone);

    remote_session_port_closed(&session);
    RemoteDisplayState display_state;
    remote_session_display(&session, &display_state);
    REMOTE_TEST_ASSERT(report, !display_state.link_connected, "not connected after a drop");
    REMOTE_TEST_ASSERT(report, display_state.payload[0] == '\0', "the passphrase is cleared on disconnect");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 1, session.reconnections, "counted");

    /* Reconnect: the display is not the stale record until a new DISPLAY. */
    remote_session_port_opened(&session);
    remote_session_display(&session, &display_state);
    REMOTE_TEST_ASSERT(report, display_state.link_connecting, "connecting, not the old record");
    REMOTE_TEST_ASSERT(report, display_state.payload[0] == '\0', "still no stale payload");
}

static void a_button_is_transmitted_only_while_connected_and_foregrounded(RemoteTestReport* report) {
    RemoteSession session;
    remote_session_initialise(&session, "flipper-zero");
    char output[256];

    /* Down link: dropped, counted. */
    REMOTE_TEST_ASSERT(report, !remote_session_report_event(&session, RemoteReportableEventCenterShort, true), "not while down");
    /* Handshaking: dropped. */
    remote_session_port_opened(&session);
    take_output(&session, output, sizeof(output));
    REMOTE_TEST_ASSERT(report, !remote_session_report_event(&session, RemoteReportableEventCenterShort, true), "not while handshaking");
    take_output(&session, output, sizeof(output));
    REMOTE_TEST_ASSERT(report, output[0] == '\0', "nothing sent while handshaking");

    /* Connected and foregrounded: sent, with both flags true. */
    receive_display(&session, RemoteProtocolStatusReady, RemoteProtocolPageNone, "", 0, RemoteProtocolErrorNone);
    REMOTE_TEST_ASSERT(report, remote_session_report_event(&session, RemoteReportableEventCenterLong, true), "sent");
    take_output(&session, output, sizeof(output));
    REMOTE_TEST_ASSERT(report, strcmp(output, "BUTTON event=CENTER_LONG foregrounded=1 unlocked=1\n") == 0, "both flags true");

    /* Connected but backgrounded: dropped by the guard, counted. */
    REMOTE_TEST_ASSERT(report, !remote_session_report_event(&session, RemoteReportableEventCenterShort, false), "not while backgrounded");
    take_output(&session, output, sizeof(output));
    REMOTE_TEST_ASSERT(report, output[0] == '\0', "nothing sent while backgrounded");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 1, session.events_dropped_by_guard, "guard drop counted");

    /* Connected but locked: dropped even if foregrounded (belt and braces). */
    remote_session_lock_changed(&session, true);
    take_output(&session, output, sizeof(output));
    REMOTE_TEST_ASSERT(report, !remote_session_report_event(&session, RemoteReportableEventCenterShort, true), "not while locked");
}

static void every_reportable_event_maps_to_its_wire_name(RemoteTestReport* report) {
    typedef struct {
        RemoteReportableEvent event;
        const char* wire;
    } EventRow;
    const EventRow rows[] = {
        {RemoteReportableEventCenterShort, "CENTER_SHORT"},
        {RemoteReportableEventCenterLong, "CENTER_LONG"},
        {RemoteReportableEventLeftShort, "LEFT_SHORT"},
        {RemoteReportableEventRightShort, "RIGHT_SHORT"},
        {RemoteReportableEventBackShort, "BACK_SHORT"},
    };
    for(int row_index = 0; row_index < REMOTE_TEST_ROW_COUNT(rows); row_index++) {
        RemoteSession session;
        remote_session_initialise(&session, "flipper-zero");
        remote_session_port_opened(&session);
        char output[256];
        take_output(&session, output, sizeof(output));
        receive_display(&session, RemoteProtocolStatusReady, RemoteProtocolPageNone, "", 0, RemoteProtocolErrorNone);
        REMOTE_TEST_ASSERT(report, remote_session_report_event(&session, rows[row_index].event, true), rows[row_index].wire);
        take_output(&session, output, sizeof(output));
        char expected[64];
        snprintf(expected, sizeof(expected), "BUTTON event=%s foregrounded=1 unlocked=1\n", rows[row_index].wire);
        REMOTE_TEST_ASSERT(report, strcmp(output, expected) == 0, rows[row_index].wire);
    }
}

static void a_button_is_not_queued_across_a_disconnection(RemoteTestReport* report) {
    RemoteSession session;
    remote_session_initialise(&session, "flipper-zero");
    remote_session_port_opened(&session);
    char output[256];
    take_output(&session, output, sizeof(output));
    receive_display(&session, RemoteProtocolStatusReady, RemoteProtocolPageNone, "", 0, RemoteProtocolErrorNone);

    /* A long press is reported but the transport has not drained it yet when
     * the cable is pulled. It must not survive to the next session. */
    REMOTE_TEST_ASSERT(report, remote_session_report_event(&session, RemoteReportableEventCenterLong, true), "reported");
    remote_session_port_closed(&session);
    take_output(&session, output, sizeof(output));
    REMOTE_TEST_ASSERT(report, output[0] == '\0', "the unsent press is discarded on disconnect");

    remote_session_port_opened(&session);
    take_output(&session, output, sizeof(output));
    REMOTE_TEST_ASSERT(report, strncmp(output, "HELLO", 5) == 0, "reconnect sends only a fresh hello");
    REMOTE_TEST_ASSERT(report, strstr(output, "CENTER_LONG") == NULL, "no replayed press");
}

static void lock_changes_send_state_only_while_connected(RemoteTestReport* report) {
    RemoteSession session;
    remote_session_initialise(&session, "flipper-zero");
    char output[256];

    /* While down, a lock change updates the display but sends nothing. */
    remote_session_lock_changed(&session, true);
    take_output(&session, output, sizeof(output));
    REMOTE_TEST_ASSERT(report, output[0] == '\0', "no state while down");
    RemoteDisplayState display_state;
    remote_session_display(&session, &display_state);
    REMOTE_TEST_ASSERT(report, display_state.screen_locked, "display shows locked");

    /* Connected: a lock change sends STATE. */
    remote_session_port_opened(&session);
    take_output(&session, output, sizeof(output));
    receive_display(&session, RemoteProtocolStatusReady, RemoteProtocolPageNone, "", 0, RemoteProtocolErrorNone);
    remote_session_lock_changed(&session, false);
    take_output(&session, output, sizeof(output));
    REMOTE_TEST_ASSERT(report, strcmp(output, "STATE locked=0\n") == 0, "state sent");
    remote_session_lock_changed(&session, true);
    take_output(&session, output, sizeof(output));
    REMOTE_TEST_ASSERT(report, strcmp(output, "STATE locked=1\n") == 0, "state sent again");
}

static void malformed_and_unexpected_verbs_are_counted_not_rendered(RemoteTestReport* report) {
    RemoteSession session;
    remote_session_initialise(&session, "flipper-zero");
    remote_session_port_opened(&session);
    char output[256];
    take_output(&session, output, sizeof(output));

    /* Garbage while handshaking does not connect the link. */
    const char* garbage = "%%% not a line %%%\n";
    remote_session_receive(&session, (const uint8_t*)garbage, strlen(garbage));
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteSessionHandshaking, session.link_state, "still handshaking");
    REMOTE_TEST_ASSERT(report, session.malformed_received >= 1, "counted");

    /* The appliance's own verb never flows to the appliance; a BUTTON from
     * the appliance side is not a DISPLAY, so it is ignored and counted. */
    remote_session_receive(&session, (const uint8_t*)"BUTTON event=CENTER_SHORT foregrounded=1 unlocked=1\n", strlen("BUTTON event=CENTER_SHORT foregrounded=1 unlocked=1\n"));
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteSessionHandshaking, session.link_state, "a button does not connect");
    REMOTE_TEST_ASSERT(report, session.malformed_received >= 2, "wrong direction counted");
}

static void the_output_buffer_drops_rather_than_overflows(RemoteTestReport* report) {
    RemoteSession session;
    remote_session_initialise(&session, "flipper-zero");
    remote_session_port_opened(&session);
    char output[256];
    take_output(&session, output, sizeof(output));
    receive_display(&session, RemoteProtocolStatusReady, RemoteProtocolPageNone, "", 0, RemoteProtocolErrorNone);

    /* Report many events without draining: past the queue depth they are
     * dropped and counted, never written past the buffer. */
    int transmitted = 0;
    for(int repeat = 0; repeat < 100; repeat++) {
        if(remote_session_report_event(&session, RemoteReportableEventCenterShort, true)) transmitted++;
    }
    REMOTE_TEST_ASSERT(report, transmitted <= (int)REMOTE_PROTOCOL_PERIPHERAL_OUTBOUND_QUEUE_DEPTH, "no more than the queue depth held");
    REMOTE_TEST_ASSERT(report, session.events_dropped_by_output_full > 0, "the rest dropped and counted");
    REMOTE_TEST_ASSERT(report, session.output_length <= sizeof(session.output), "within the buffer");
}

static void an_invalid_token_is_replaced_with_a_safe_default(RemoteTestReport* report) {
    RemoteSession session;
    remote_session_initialise(&session, "Flipper Zero!");
    remote_session_port_opened(&session);
    char output[256];
    take_output(&session, output, sizeof(output));
    /* The HELLO still parses: the token was replaced, not sent invalid. */
    RemoteProtocolMessage decoded;
    RemoteProtocolFeedOutcome outcome = remote_protocol_parse_line(output, strlen(output) - 1, &decoded);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteProtocolFeedOutcomeMessage, outcome.kind, "a valid hello is still sent");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteProtocolVerbHello, decoded.verb, "hello");
}

int main(void) {
    static const RemoteTestCase test_cases[] = {
        {"a fresh session is down and silent", a_fresh_session_is_down_and_silent},
        {"opening the port sends hello and begins handshake", opening_the_port_sends_hello_and_begins_handshake},
        {"hello carries the current lock state", hello_carries_the_current_lock_state},
        {"the first display completes the handshake", the_first_display_completes_the_handshake},
        {"a bad version answer marks the link incompatible", a_bad_version_answer_marks_the_link_incompatible},
        {"a display replaces the previous one wholly", a_display_replaces_the_previous_one_wholly},
        {"a disconnect mid message discards the partial line", a_disconnect_mid_message_discards_the_partial_line},
        {"reconnect discards local state and clears the payload", reconnect_discards_local_state_and_clears_the_payload},
        {"a button is transmitted only while connected and foregrounded", a_button_is_transmitted_only_while_connected_and_foregrounded},
        {"every reportable event maps to its wire name", every_reportable_event_maps_to_its_wire_name},
        {"a button is not queued across a disconnection", a_button_is_not_queued_across_a_disconnection},
        {"lock changes send state only while connected", lock_changes_send_state_only_while_connected},
        {"malformed and unexpected verbs are counted not rendered", malformed_and_unexpected_verbs_are_counted_not_rendered},
        {"the output buffer drops rather than overflows", the_output_buffer_drops_rather_than_overflows},
        {"an invalid token is replaced with a safe default", an_invalid_token_is_replaced_with_a_safe_default},
    };
    return remote_test_run_all(test_cases, REMOTE_TEST_ROW_COUNT(test_cases));
}
