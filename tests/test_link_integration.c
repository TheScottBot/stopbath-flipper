/*
 * FE4 integration: the real session and the real development peer, wired
 * together over a byte pipe, exercised through the Part 7 prototype sequence
 * and through the connect and disconnect cycle the hardware gate performs by
 * hand. Nothing here is SDK bound, so it stands in software for the logic the
 * cable pull test checks physically (which remains the author's on hardware).
 */
#include "test_support.h"

#include "../peer/development_peer_core.h"
#include "../session/remote_session.h"

/* Moves every byte each side has queued to the other, until both are quiet.
 * This is the wire, with no loss and no reordering. */
static void pump(RemoteSession* session, DevelopmentPeerCore* peer) {
    uint8_t buffer[512];
    for(int guard = 0; guard < 100; guard++) {
        size_t from_session = remote_session_take_output(session, buffer, sizeof(buffer));
        if(from_session > 0) development_peer_feed(peer, buffer, from_session);
        size_t from_peer = development_peer_take_output(peer, buffer, sizeof(buffer));
        if(from_peer > 0) remote_session_receive(session, buffer, from_peer);
        if(from_session == 0 && from_peer == 0) return;
    }
}

static RemoteDisplayStatus display_status(const RemoteSession* session) {
    RemoteDisplayState display_state;
    remote_session_display(session, &display_state);
    return (RemoteDisplayStatus)display_state.status;
}

static void the_handshake_connects_both_sides(RemoteTestReport* report) {
    RemoteSession session;
    remote_session_initialise(&session, "flipper-zero");
    DevelopmentPeerCore peer;
    development_peer_initialise(&peer);

    remote_session_port_opened(&session);
    pump(&session, &peer);

    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteSessionConnected, session.link_state, "session connected");
    REMOTE_TEST_ASSERT(report, peer.handshake_complete, "peer handshook");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 1, peer.hellos_received, "one hello");
    REMOTE_TEST_ASSERT(report, strcmp(peer.peer_token, "flipper-zero") == 0, "token received");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteDisplayStatusReady, display_status(&session), "session renders the peer's idle record");
}

static void a_state_the_operator_drives_reaches_the_session(RemoteTestReport* report) {
    RemoteSession session;
    remote_session_initialise(&session, "flipper-zero");
    DevelopmentPeerCore peer;
    development_peer_initialise(&peer);
    remote_session_port_opened(&session);
    pump(&session, &peer);

    development_peer_set_display(&peer, RemoteProtocolStatusPresenting, RemoteProtocolPageWifi, "WIFI:T:WPA;S:a;P:b;;", 0, RemoteProtocolErrorNone);
    pump(&session, &peer);
    RemoteDisplayState display_state;
    remote_session_display(&session, &display_state);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteDisplayStatusPresenting, display_state.status, "presenting");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteDisplayPageWifi, display_state.page, "wifi page");
    REMOTE_TEST_ASSERT(report, strcmp(display_state.payload, "WIFI:T:WPA;S:a;P:b;;") == 0, "payload rendered");
}

static void a_button_reaches_the_peer(RemoteTestReport* report) {
    RemoteSession session;
    remote_session_initialise(&session, "flipper-zero");
    DevelopmentPeerCore peer;
    development_peer_initialise(&peer);
    remote_session_port_opened(&session);
    pump(&session, &peer);

    REMOTE_TEST_ASSERT(report, remote_session_report_event(&session, RemoteReportableEventCenterLong, true), "reported");
    pump(&session, &peer);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 1, peer.buttons_received, "the peer received the button");
    const DevelopmentPeerLogEntry* last = development_peer_log_entry(&peer, peer.log_count - 1);
    REMOTE_TEST_ASSERT(report, strcmp(last->line, "BUTTON event=CENTER_LONG foregrounded=1 unlocked=1") == 0, "as sent");
}

static void the_demo_script_walks_the_prototype_sequence(RemoteTestReport* report) {
    /* Part 7: short centre starts, left and right switch pages, long centre
     * ends. Driven end to end through the real bytes. */
    RemoteSession session;
    remote_session_initialise(&session, "flipper-zero");
    DevelopmentPeerCore peer;
    development_peer_initialise(&peer);
    peer.behaviour.demo_script = true;
    remote_session_port_opened(&session);
    pump(&session, &peer);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteDisplayStatusReady, display_status(&session), "idle");

    remote_session_report_event(&session, RemoteReportableEventCenterShort, true);
    pump(&session, &peer);
    RemoteDisplayState display_state;
    remote_session_display(&session, &display_state);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteDisplayStatusPresenting, display_state.status, "started");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteDisplayPageWifi, display_state.page, "wifi first");

    remote_session_report_event(&session, RemoteReportableEventRightShort, true);
    pump(&session, &peer);
    remote_session_display(&session, &display_state);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteDisplayPageGuest, display_state.page, "right to gallery");

    remote_session_report_event(&session, RemoteReportableEventLeftShort, true);
    pump(&session, &peer);
    remote_session_display(&session, &display_state);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteDisplayPageWifi, display_state.page, "left to wifi");

    remote_session_report_event(&session, RemoteReportableEventCenterLong, true);
    pump(&session, &peer);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteDisplayStatusReady, display_status(&session), "ended");
}

static void reconnect_always_results_in_peer_supplied_state(RemoteTestReport* report) {
    /* The software mirror of the twenty cable pulls: each cycle the session
     * must come back showing the peer's current record, never stale content,
     * and no button pressed during a gap may cross it. */
    RemoteSession session;
    remote_session_initialise(&session, "flipper-zero");
    DevelopmentPeerCore peer;
    development_peer_initialise(&peer);
    remote_session_port_opened(&session);
    pump(&session, &peer);
    development_peer_set_display(&peer, RemoteProtocolStatusGuestConnected, RemoteProtocolPageGuest, "HTTP://192.168.72.1/", 4, RemoteProtocolErrorNone);
    pump(&session, &peer);

    for(int cycle = 0; cycle < 20; cycle++) {
        /* A press arrives while the cable is being pulled: reported, then the
         * link drops before it is pumped across. */
        remote_session_report_event(&session, RemoteReportableEventCenterLong, true);
        remote_session_port_closed(&session);
        /* The peer sees the cable gone too and discards its link state. */
        development_peer_link_dropped(&peer);

        /* Nothing crosses the gap. */
        uint8_t buffer[512];
        size_t leaked = remote_session_take_output(&session, buffer, sizeof(buffer));
        REMOTE_TEST_ASSERT_EQUAL_INT(report, 0, leaked, "no unsent bytes survive the drop");

        RemoteDisplayState during;
        remote_session_display(&session, &during);
        REMOTE_TEST_ASSERT(report, !during.link_connected, "not connected during the gap");
        REMOTE_TEST_ASSERT(report, during.payload[0] == '\0', "no stale payload during the gap");

        /* Reinsert: the session re-handshakes and the peer answers with its
         * current record, which is what the session must then show. */
        remote_session_port_opened(&session);
        pump(&session, &peer);
        REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteSessionConnected, session.link_state, "reconnected");
        RemoteDisplayState after;
        remote_session_display(&session, &after);
        REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteDisplayStatusGuestConnected, after.status, "peer supplied state");
        REMOTE_TEST_ASSERT_EQUAL_INT(report, 4, after.delivered_count, "the peer's count, not a stale one");
        REMOTE_TEST_ASSERT(report, strcmp(after.payload, "HTTP://192.168.72.1/") == 0, "the peer's payload");
    }
    /* The peer never acted on a button pressed during a gap. */
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 0, peer.buttons_received, "no press crossed any gap");
    /* One handshake at first connect, then one per reconnection. */
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 21, peer.hellos_received, "one handshake per connection");
}

static void an_incompatible_peer_is_shown_incompatible(RemoteTestReport* report) {
    RemoteSession session;
    remote_session_initialise(&session, "flipper-zero");
    DevelopmentPeerCore peer;
    development_peer_initialise(&peer);
    peer.behaviour.reject_every_version = true;
    remote_session_port_opened(&session);
    pump(&session, &peer);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteSessionIncompatible, session.link_state, "incompatible");
    RemoteDisplayState display_state;
    remote_session_display(&session, &display_state);
    REMOTE_TEST_ASSERT(report, display_state.link_incompatible, "shown incompatible");
}

static void a_misbehaving_peer_does_not_connect_the_session(RemoteTestReport* report) {
    RemoteSession session;
    remote_session_initialise(&session, "flipper-zero");
    DevelopmentPeerCore peer;
    development_peer_initialise(&peer);
    remote_session_port_opened(&session);
    /* Drain the session's HELLO so the peer would answer, but instead the peer
     * emits each misbehaviour before any valid record. */
    uint8_t buffer[512];
    remote_session_take_output(&session, buffer, sizeof(buffer));

    development_peer_send_malformed(&peer);
    development_peer_send_oversized(&peer);
    development_peer_send_partial_record(&peer);
    /* Drain the whole burst, in wire sized chunks, not just the first. */
    for(size_t chunk = development_peer_take_output(&peer, buffer, sizeof(buffer)); chunk > 0;
        chunk = development_peer_take_output(&peer, buffer, sizeof(buffer))) {
        remote_session_receive(&session, buffer, chunk);
    }
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteSessionHandshaking, session.link_state, "still handshaking, not connected on garbage");
    REMOTE_TEST_ASSERT(report, session.malformed_received >= 2, "garbage counted");
}

int main(void) {
    static const RemoteTestCase test_cases[] = {
        {"the handshake connects both sides", the_handshake_connects_both_sides},
        {"a state the operator drives reaches the session", a_state_the_operator_drives_reaches_the_session},
        {"a button reaches the peer", a_button_reaches_the_peer},
        {"the demo script walks the prototype sequence", the_demo_script_walks_the_prototype_sequence},
        {"reconnect always results in peer supplied state", reconnect_always_results_in_peer_supplied_state},
        {"an incompatible peer is shown incompatible", an_incompatible_peer_is_shown_incompatible},
        {"a misbehaving peer does not connect the session", a_misbehaving_peer_does_not_connect_the_session},
    };
    return remote_test_run_all(test_cases, REMOTE_TEST_ROW_COUNT(test_cases));
}
