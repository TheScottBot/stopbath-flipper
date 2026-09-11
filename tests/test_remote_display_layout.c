/*
 * FE2 tests first: state to layout mapping for every state, unknown status
 * renders a safe fallback, long values truncate rather than overflow.
 */
#include "test_support.h"

#include "../remote_display/remote_display_fixtures.h"
#include "../remote_display/remote_display_layout.h"

static const RemoteDisplayState* fixture_named(const char* fixture_name) {
    const RemoteDisplayFixture* fixtures = remote_display_fixtures();
    for(int fixture_index = 0; fixture_index < remote_display_fixture_count(); fixture_index++) {
        if(strcmp(fixtures[fixture_index].fixture_name, fixture_name) == 0) {
            return &fixtures[fixture_index].display_state;
        }
    }
    return NULL;
}

static RemoteDisplayLayout compose_named(RemoteTestReport* report, const char* fixture_name) {
    RemoteDisplayLayout layout;
    const RemoteDisplayState* display_state = fixture_named(fixture_name);
    REMOTE_TEST_ASSERT(report, display_state != NULL, fixture_name);
    if(display_state != NULL) {
        remote_display_layout_compose(display_state, &layout);
    } else {
        memset(&layout, 0, sizeof(layout));
    }
    return layout;
}

typedef struct {
    const char* text;
    int x;
    int y;
    RemoteLayoutFont font;
    RemoteLayoutAnchor anchor;
    bool inverted;
} ExpectedText;

static void assert_text_placed(RemoteTestReport* report, const RemoteDisplayLayout* layout, ExpectedText expected) {
    const RemoteLayoutText* placed = remote_display_layout_find_text(layout, expected.text);
    REMOTE_TEST_ASSERT(report, placed != NULL, expected.text);
    if(placed == NULL) return;
    REMOTE_TEST_ASSERT_EQUAL_INT(report, expected.x, placed->x, expected.text);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, expected.y, placed->y, expected.text);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, expected.font, placed->font, expected.text);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, expected.anchor, placed->anchor, expected.text);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, expected.inverted, placed->inverted, expected.text);
}

static void assert_text_absent(RemoteTestReport* report, const RemoteDisplayLayout* layout, const char* text) {
    REMOTE_TEST_ASSERT(report, remote_display_layout_find_text(layout, text) == NULL, text);
}

static bool shape_present(const RemoteDisplayLayout* layout, RemoteLayoutShapeKind kind, int x, int y, int width, int height) {
    for(int shape_index = 0; shape_index < layout->shape_count; shape_index++) {
        const RemoteLayoutShape* shape = &layout->shapes[shape_index];
        if(shape->kind == kind && shape->x == x && shape->y == y && shape->width == width && shape->height == height) {
            return true;
        }
    }
    return false;
}

static const ExpectedText header_text = {"StopBath", 2, REMOTE_LAYOUT_HEADER_BASELINE, RemoteLayoutFontPrimary, RemoteLayoutAnchorLeft, false};

static void every_fixture_composes_within_bounds_with_a_header(RemoteTestReport* report) {
    const RemoteDisplayFixture* fixtures = remote_display_fixtures();
    for(int fixture_index = 0; fixture_index < remote_display_fixture_count(); fixture_index++) {
        const RemoteDisplayFixture* fixture = &fixtures[fixture_index];
        RemoteDisplayLayout layout;
        remote_display_layout_compose(&fixture->display_state, &layout);
        REMOTE_TEST_ASSERT(report, layout.shape_count >= 0 && layout.shape_count <= REMOTE_LAYOUT_MAX_SHAPES, fixture->fixture_name);
        REMOTE_TEST_ASSERT(report, layout.text_count >= 1 && layout.text_count <= REMOTE_LAYOUT_MAX_TEXTS, fixture->fixture_name);
        /* The header names the product, or says LOCKED where there is no
         * room for both beside a code. */
        bool header_present = remote_display_layout_find_text(&layout, "StopBath") != NULL ||
                              remote_display_layout_find_text(&layout, "LOCKED") != NULL;
        REMOTE_TEST_ASSERT(report, header_present, fixture->fixture_name);
        for(int text_index = 0; text_index < layout.text_count; text_index++) {
            const RemoteLayoutText* text = &layout.texts[text_index];
            REMOTE_TEST_ASSERT(report, strlen(text->text) < REMOTE_LAYOUT_TEXT_CAPACITY, "every text is terminated inside its buffer");
            REMOTE_TEST_ASSERT(report, text->x >= 0 && text->x < REMOTE_DISPLAY_WIDTH, "text x on screen");
            REMOTE_TEST_ASSERT(report, text->y >= 0 && text->y < REMOTE_DISPLAY_HEIGHT, "text y on screen");
        }
    }
}

static void connecting_shows_a_distinct_screen(RemoteTestReport* report) {
    RemoteDisplayState display_state = *fixture_named("presenting wifi");
    display_state.link_connected = false;
    display_state.link_connecting = true;
    RemoteDisplayLayout layout;
    remote_display_layout_compose(&display_state, &layout);
    assert_text_placed(report, &layout, header_text);
    assert_text_placed(report, &layout, (ExpectedText){"Connecting", 64, 34, RemoteLayoutFontPrimary, RemoteLayoutAnchorCenter, false});
    assert_text_placed(report, &layout, (ExpectedText){"Please wait", 64, 50, RemoteLayoutFontSecondary, RemoteLayoutAnchorCenter, false});
    REMOTE_TEST_ASSERT(report, !layout.qr_area_shown, "no code while connecting: not confirmed yet");
    assert_text_absent(report, &layout, "Pi disconnected");
}

static void an_incompatible_link_shows_a_distinct_screen(RemoteTestReport* report) {
    RemoteDisplayState display_state = *fixture_named("presenting wifi");
    display_state.link_connected = false;
    /* Incompatible wins even if connecting is also set. */
    display_state.link_connecting = true;
    display_state.link_incompatible = true;
    RemoteDisplayLayout layout;
    remote_display_layout_compose(&display_state, &layout);
    assert_text_placed(report, &layout, (ExpectedText){"Incompatible", 64, 34, RemoteLayoutFontPrimary, RemoteLayoutAnchorCenter, false});
    assert_text_placed(report, &layout, (ExpectedText){"Update remote", 64, 50, RemoteLayoutFontSecondary, RemoteLayoutAnchorCenter, false});
    REMOTE_TEST_ASSERT(report, !layout.qr_area_shown, "no code when incompatible");
    assert_text_absent(report, &layout, "Connecting");
    assert_text_absent(report, &layout, "Pi disconnected");
}

static void not_connected_overrides_whatever_the_record_says(RemoteTestReport* report) {
    RemoteDisplayLayout layout = compose_named(report, "not connected");
    assert_text_placed(report, &layout, header_text);
    assert_text_placed(report, &layout, (ExpectedText){"Pi disconnected", 64, 34, RemoteLayoutFontPrimary, RemoteLayoutAnchorCenter, false});
    assert_text_placed(report, &layout, (ExpectedText){"Reconnect USB", 64, 50, RemoteLayoutFontSecondary, RemoteLayoutAnchorCenter, false});
    REMOTE_TEST_ASSERT(report, !layout.qr_area_shown, "no code while disconnected: it may be stale");
    REMOTE_TEST_ASSERT(report, !layout.center_button_shown, "nothing to press while disconnected");
    assert_text_absent(report, &layout, "Presenting");
    assert_text_absent(report, &layout, "2 delivered");
}

static void ready_shows_the_word_and_the_new_session_hint(RemoteTestReport* report) {
    RemoteDisplayLayout layout = compose_named(report, "ready");
    assert_text_placed(report, &layout, header_text);
    assert_text_placed(report, &layout, (ExpectedText){"Ready", 64, 34, RemoteLayoutFontPrimary, RemoteLayoutAnchorCenter, false});
    REMOTE_TEST_ASSERT(report, layout.center_button_shown, "centre button hint shown");
    REMOTE_TEST_ASSERT(report, strcmp(layout.center_button_label, "New session") == 0, "hint text");
    REMOTE_TEST_ASSERT(report, !layout.qr_area_shown, "no code when idle");
    REMOTE_TEST_ASSERT(report, !shape_present(&layout, RemoteLayoutShapeFilledBox, 0, 0, 128, 12), "header not inverted when unlocked");
}

/* Nothing may be drawn over the code: the person scanning it is not the
 * person holding the device, and a stray header row makes it unscannable. */
static void assert_nothing_overlaps_the_code_area(RemoteTestReport* report, const RemoteDisplayLayout* layout) {
    int code_right = layout->qr_x + layout->qr_size;
    int code_bottom = layout->qr_y + layout->qr_size;
    for(int text_index = 0; text_index < layout->text_count; text_index++) {
        const RemoteLayoutText* text = &layout->texts[text_index];
        /* Every text on a code page is anchored left in the column, so x is
         * its left edge; a right anchored text would end at x. */
        int text_left = text->anchor == RemoteLayoutAnchorLeft ? text->x : text->x - text->max_width_pixels;
        REMOTE_TEST_ASSERT(report, text_left >= code_right, text->text);
    }
    for(int shape_index = 0; shape_index < layout->shape_count; shape_index++) {
        const RemoteLayoutShape* shape = &layout->shapes[shape_index];
        bool clear_of_code = shape->x >= code_right || shape->y >= code_bottom;
        REMOTE_TEST_ASSERT(report, clear_of_code, "a shape stays clear of the code");
    }
}

static void presenting_wifi_places_the_code_and_the_column(RemoteTestReport* report) {
    RemoteDisplayLayout layout = compose_named(report, "presenting wifi");
    REMOTE_TEST_ASSERT(report, layout.qr_area_shown, "code area shown");
    assert_text_placed(report, &layout, (ExpectedText){"StopBath", REMOTE_LAYOUT_COLUMN_X, REMOTE_LAYOUT_HEADER_BASELINE, RemoteLayoutFontPrimary, RemoteLayoutAnchorLeft, false});
    assert_nothing_overlaps_the_code_area(report, &layout);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, REMOTE_LAYOUT_QR_X, layout.qr_x, "code x");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, REMOTE_LAYOUT_QR_Y, layout.qr_y, "code y");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, REMOTE_LAYOUT_QR_SIZE, layout.qr_size, "code size");
    assert_text_placed(report, &layout, (ExpectedText){"Wi-Fi", REMOTE_LAYOUT_COLUMN_X, 23, RemoteLayoutFontPrimary, RemoteLayoutAnchorLeft, false});
    assert_text_placed(report, &layout, (ExpectedText){"<  1/2  >", REMOTE_LAYOUT_COLUMN_X, 34, RemoteLayoutFontSecondary, RemoteLayoutAnchorLeft, false});
    assert_text_placed(report, &layout, (ExpectedText){"Presenting", REMOTE_LAYOUT_COLUMN_X, 45, RemoteLayoutFontSecondary, RemoteLayoutAnchorLeft, false});
    assert_text_absent(report, &layout, "0 delivered");
    REMOTE_TEST_ASSERT(report, !layout.center_button_shown, "no hint on a code page");
}

static void presenting_guest_is_page_two_titled_gallery(RemoteTestReport* report) {
    RemoteDisplayLayout layout = compose_named(report, "presenting guest");
    REMOTE_TEST_ASSERT(report, layout.qr_area_shown, "code area shown");
    assert_text_placed(report, &layout, (ExpectedText){"Gallery", REMOTE_LAYOUT_COLUMN_X, 23, RemoteLayoutFontPrimary, RemoteLayoutAnchorLeft, false});
    assert_text_placed(report, &layout, (ExpectedText){"<  2/2  >", REMOTE_LAYOUT_COLUMN_X, 34, RemoteLayoutFontSecondary, RemoteLayoutAnchorLeft, false});
}

static void guest_connected_keeps_the_code_and_changes_the_status_line(RemoteTestReport* report) {
    RemoteDisplayLayout wifi = compose_named(report, "guest connected wifi");
    REMOTE_TEST_ASSERT(report, wifi.qr_area_shown, "code stays on screen when a guest joins");
    assert_text_placed(report, &wifi, (ExpectedText){"Guest joined", REMOTE_LAYOUT_COLUMN_X, 45, RemoteLayoutFontSecondary, RemoteLayoutAnchorLeft, false});
    assert_text_absent(report, &wifi, "Presenting");

    RemoteDisplayLayout guest = compose_named(report, "guest connected guest three delivered");
    REMOTE_TEST_ASSERT(report, guest.qr_area_shown, "code stays on screen when a photograph lands");
    assert_text_placed(report, &guest, (ExpectedText){"3 delivered", REMOTE_LAYOUT_COLUMN_X, 56, RemoteLayoutFontSecondary, RemoteLayoutAnchorLeft, false});
}

static void terminating_and_recovery_are_two_centred_lines_without_a_code(RemoteTestReport* report) {
    RemoteDisplayLayout terminating = compose_named(report, "terminating");
    assert_text_placed(report, &terminating, (ExpectedText){"Ending session", 64, 34, RemoteLayoutFontPrimary, RemoteLayoutAnchorCenter, false});
    assert_text_placed(report, &terminating, (ExpectedText){"Please wait", 64, 50, RemoteLayoutFontSecondary, RemoteLayoutAnchorCenter, false});
    REMOTE_TEST_ASSERT(report, !terminating.qr_area_shown, "no code while terminating");
    REMOTE_TEST_ASSERT(report, !terminating.center_button_shown, "nothing accepted while terminating");
    assert_text_absent(report, &terminating, "3 delivered");

    RemoteDisplayLayout recovery = compose_named(report, "recovery required");
    assert_text_placed(report, &recovery, (ExpectedText){"Recovery needed", 64, 34, RemoteLayoutFontPrimary, RemoteLayoutAnchorCenter, false});
    assert_text_placed(report, &recovery, (ExpectedText){"See dashboard", 64, 50, RemoteLayoutFontSecondary, RemoteLayoutAnchorCenter, false});
    REMOTE_TEST_ASSERT(report, !recovery.qr_area_shown, "no code in recovery");
}

static void the_status_decides_the_screen_not_the_page(RemoteTestReport* report) {
    /* A page named while idle, terminating or in recovery is a record the
     * appliance should never send; if it does, the status wins and the page
     * is not shown. */
    RemoteDisplayState display_state = *fixture_named("ready");
    display_state.page = RemoteDisplayPageWifi;
    snprintf(display_state.payload, sizeof(display_state.payload), "%s", "WIFI:T:WPA;S:fixture;P:fixture;;");
    RemoteDisplayLayout layout;
    remote_display_layout_compose(&display_state, &layout);
    REMOTE_TEST_ASSERT(report, !layout.qr_area_shown, "idle never shows a code");
    assert_text_placed(report, &layout, (ExpectedText){"Ready", 64, 34, RemoteLayoutFontPrimary, RemoteLayoutAnchorCenter, false});
}

static void an_active_status_with_no_page_falls_back_to_a_centred_status(RemoteTestReport* report) {
    RemoteDisplayState display_state = *fixture_named("presenting wifi");
    display_state.page = RemoteDisplayPageNone;
    display_state.payload[0] = '\0';
    RemoteDisplayLayout layout;
    remote_display_layout_compose(&display_state, &layout);
    REMOTE_TEST_ASSERT(report, !layout.qr_area_shown, "no page, no code");
    assert_text_placed(report, &layout, (ExpectedText){"Presenting", 64, 34, RemoteLayoutFontPrimary, RemoteLayoutAnchorCenter, false});
}

static void a_page_with_an_empty_payload_still_frames_the_code_area(RemoteTestReport* report) {
    RemoteDisplayState display_state = *fixture_named("presenting wifi");
    display_state.payload[0] = '\0';
    RemoteDisplayLayout layout;
    remote_display_layout_compose(&display_state, &layout);
    REMOTE_TEST_ASSERT(report, layout.qr_area_shown, "the frame is drawn even with nothing to encode");
}

static void an_error_is_an_inverted_band_at_the_bottom(RemoteTestReport* report) {
    RemoteDisplayLayout ready = compose_named(report, "ready with error");
    REMOTE_TEST_ASSERT(report, shape_present(&ready, RemoteLayoutShapeFilledBox, 0, REMOTE_LAYOUT_ERROR_BAND_Y, 128, REMOTE_LAYOUT_ERROR_BAND_HEIGHT), "band box");
    assert_text_placed(report, &ready, (ExpectedText){REMOTE_DISPLAY_FIXTURE_ERROR_CODE, 64, 61, RemoteLayoutFontSecondary, RemoteLayoutAnchorCenter, true});
    REMOTE_TEST_ASSERT(report, !ready.center_button_shown, "the band takes the button hint's row");

    RemoteDisplayLayout guest = compose_named(report, "guest connected with error");
    REMOTE_TEST_ASSERT(report, guest.qr_area_shown, "an error never removes the code");
    /* Beside a code the band is column width, in the delivered line's row,
     * so it cannot cross the code. */
    REMOTE_TEST_ASSERT(report, shape_present(&guest, RemoteLayoutShapeFilledBox, REMOTE_LAYOUT_COLUMN_X - 2, REMOTE_LAYOUT_ERROR_BAND_Y, REMOTE_DISPLAY_WIDTH - (REMOTE_LAYOUT_COLUMN_X - 2), REMOTE_LAYOUT_ERROR_BAND_HEIGHT), "column band");
    /* The column band holds about eleven upper case characters (measured in
     * the evaluation log 4.4), so a longer code shows its start. */
    const RemoteLayoutText* column_band_text = NULL;
    for(int text_index = 0; text_index < guest.text_count; text_index++) {
        if(guest.texts[text_index].inverted && guest.texts[text_index].y == 61) {
            column_band_text = &guest.texts[text_index];
        }
    }
    REMOTE_TEST_ASSERT(report, column_band_text != NULL, "column band text present");
    if(column_band_text != NULL) {
        REMOTE_TEST_ASSERT_EQUAL_INT(report, REMOTE_LAYOUT_COLUMN_X, column_band_text->x, "column band text x");
        REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteLayoutAnchorLeft, column_band_text->anchor, "column band text anchor");
        REMOTE_TEST_ASSERT(report, strlen(column_band_text->text) > 0, "something of the code is shown");
        REMOTE_TEST_ASSERT(report, strncmp(column_band_text->text, REMOTE_DISPLAY_FIXTURE_ERROR_CODE, strlen(column_band_text->text)) == 0, "what is shown is the start of the code");
    }
    assert_text_absent(report, &guest, "5 delivered");
    assert_nothing_overlaps_the_code_area(report, &guest);
}

static void no_error_means_no_band(RemoteTestReport* report) {
    RemoteDisplayLayout layout = compose_named(report, "ready");
    REMOTE_TEST_ASSERT(report, !shape_present(&layout, RemoteLayoutShapeFilledBox, 0, REMOTE_LAYOUT_ERROR_BAND_Y, 128, REMOTE_LAYOUT_ERROR_BAND_HEIGHT), "no band");
}

static void locked_inverts_the_full_header_on_a_screen_without_a_code(RemoteTestReport* report) {
    RemoteDisplayLayout layout = compose_named(report, "ready locked");
    REMOTE_TEST_ASSERT(report, shape_present(&layout, RemoteLayoutShapeFilledBox, 0, 0, 128, REMOTE_LAYOUT_HEADER_HEIGHT), "full width band");
    assert_text_placed(report, &layout, (ExpectedText){"StopBath", 2, REMOTE_LAYOUT_HEADER_BASELINE, RemoteLayoutFontPrimary, RemoteLayoutAnchorLeft, true});
    assert_text_placed(report, &layout, (ExpectedText){"LOCKED", 126, REMOTE_LAYOUT_HEADER_BASELINE, RemoteLayoutFontPrimary, RemoteLayoutAnchorRight, true});
}

static void locked_inverts_only_the_column_header_beside_a_code(RemoteTestReport* report) {
    /* A full width band would cross the code; the band and the word stay in
     * the column, and the product name gives way to the word that matters. */
    RemoteDisplayLayout layout = compose_named(report, "presenting wifi locked");
    REMOTE_TEST_ASSERT(report, layout.qr_area_shown, "locking does not hide the code being presented");
    REMOTE_TEST_ASSERT(report, !shape_present(&layout, RemoteLayoutShapeFilledBox, 0, 0, 128, REMOTE_LAYOUT_HEADER_HEIGHT), "no full width band over the code");
    REMOTE_TEST_ASSERT(report, shape_present(&layout, RemoteLayoutShapeFilledBox, REMOTE_LAYOUT_COLUMN_X - 2, 0, REMOTE_DISPLAY_WIDTH - (REMOTE_LAYOUT_COLUMN_X - 2), REMOTE_LAYOUT_HEADER_HEIGHT), "column band");
    assert_text_placed(report, &layout, (ExpectedText){"LOCKED", REMOTE_LAYOUT_COLUMN_X, REMOTE_LAYOUT_HEADER_BASELINE, RemoteLayoutFontPrimary, RemoteLayoutAnchorLeft, true});
    assert_text_absent(report, &layout, "StopBath");
    assert_nothing_overlaps_the_code_area(report, &layout);

    RemoteDisplayLayout unlocked = compose_named(report, "presenting wifi");
    assert_text_absent(report, &unlocked, "LOCKED");
}

static void the_nfc_marker_shows_in_the_column_header_only_while_presenting(RemoteTestReport* report) {
    RemoteDisplayState presenting = *fixture_named("presenting wifi");
    presenting.nfc_presenting = true;
    RemoteDisplayLayout layout;
    remote_display_layout_compose(&presenting, &layout);
    assert_text_placed(report, &layout, (ExpectedText){"NFC", REMOTE_DISPLAY_WIDTH - 2, REMOTE_LAYOUT_HEADER_BASELINE, RemoteLayoutFontSecondary, RemoteLayoutAnchorRight, false});
    assert_text_placed(report, &layout, (ExpectedText){"StopBath", REMOTE_LAYOUT_COLUMN_X, REMOTE_LAYOUT_HEADER_BASELINE, RemoteLayoutFontPrimary, RemoteLayoutAnchorLeft, false});
    assert_nothing_overlaps_the_code_area(report, &layout);

    presenting.screen_locked = true;
    remote_display_layout_compose(&presenting, &layout);
    assert_text_placed(report, &layout, (ExpectedText){"NFC", REMOTE_DISPLAY_WIDTH - 2, REMOTE_LAYOUT_HEADER_BASELINE, RemoteLayoutFontSecondary, RemoteLayoutAnchorRight, true});
    assert_text_placed(report, &layout, (ExpectedText){"LOCKED", REMOTE_LAYOUT_COLUMN_X, REMOTE_LAYOUT_HEADER_BASELINE, RemoteLayoutFontPrimary, RemoteLayoutAnchorLeft, true});

    RemoteDisplayLayout unmarked = compose_named(report, "presenting wifi");
    assert_text_absent(report, &unmarked, "NFC");

    /* The flag means nothing off a code page; a stale value must not draw. */
    RemoteDisplayState ready = *fixture_named("ready");
    ready.nfc_presenting = true;
    remote_display_layout_compose(&ready, &layout);
    assert_text_absent(report, &layout, "NFC");
}

static void an_unknown_status_renders_a_safe_fallback(RemoteTestReport* report) {
    RemoteDisplayLayout layout = compose_named(report, "unknown status");
    assert_text_placed(report, &layout, header_text);
    assert_text_placed(report, &layout, (ExpectedText){"Unknown status", 64, 34, RemoteLayoutFontPrimary, RemoteLayoutAnchorCenter, false});
    REMOTE_TEST_ASSERT(report, !layout.qr_area_shown, "no code for an unknown status");
    REMOTE_TEST_ASSERT(report, !layout.center_button_shown, "no hint for an unknown status");

    RemoteDisplayState negative = *fixture_named("ready");
    negative.status = -1;
    remote_display_layout_compose(&negative, &layout);
    assert_text_placed(report, &layout, (ExpectedText){"Unknown status", 64, 34, RemoteLayoutFontPrimary, RemoteLayoutAnchorCenter, false});
}

static void an_unknown_page_is_treated_as_no_page(RemoteTestReport* report) {
    RemoteDisplayState display_state = *fixture_named("presenting wifi");
    display_state.page = RemoteDisplayPageCount + 7;
    RemoteDisplayLayout layout;
    remote_display_layout_compose(&display_state, &layout);
    REMOTE_TEST_ASSERT(report, !layout.qr_area_shown, "unknown page shows no code");
    assert_text_placed(report, &layout, (ExpectedText){"Presenting", 64, 34, RemoteLayoutFontPrimary, RemoteLayoutAnchorCenter, false});
}

static void long_values_truncate_rather_than_overflow(RemoteTestReport* report) {
    RemoteDisplayState display_state = *fixture_named("ready");
    /* Fill the error code to its capacity: 32 characters, no terminator room
     * to spare, which is the widest value the wire will ever carry. */
    memset(display_state.error_code, 'W', REMOTE_DISPLAY_ERROR_CODE_CAPACITY - 1);
    display_state.error_code[REMOTE_DISPLAY_ERROR_CODE_CAPACITY - 1] = '\0';
    RemoteDisplayLayout layout;
    remote_display_layout_compose(&display_state, &layout);

    const RemoteLayoutText* band_text = NULL;
    for(int text_index = 0; text_index < layout.text_count; text_index++) {
        if(layout.texts[text_index].inverted && layout.texts[text_index].y == 61) {
            band_text = &layout.texts[text_index];
        }
    }
    REMOTE_TEST_ASSERT(report, band_text != NULL, "band text present");
    if(band_text == NULL) return;
    size_t shown_length = strlen(band_text->text);
    REMOTE_TEST_ASSERT(report, shown_length < REMOTE_DISPLAY_ERROR_CODE_CAPACITY - 1, "truncated below the input length");
    REMOTE_TEST_ASSERT(report, shown_length < REMOTE_LAYOUT_TEXT_CAPACITY, "inside the text buffer");
    REMOTE_TEST_ASSERT(report, band_text->max_width_pixels <= REMOTE_DISPLAY_WIDTH, "fits the screen width");
    REMOTE_TEST_ASSERT(report, strncmp(band_text->text, "WWWW", 4) == 0, "truncation keeps the start, which carries the meaning");
}

static void many_delivered_is_capped_so_it_fits_the_column(RemoteTestReport* report) {
    RemoteDisplayLayout layout = compose_named(report, "many delivered");
    assert_text_placed(report, &layout, (ExpectedText){"999+ delivered", REMOTE_LAYOUT_COLUMN_X, 56, RemoteLayoutFontSecondary, RemoteLayoutAnchorLeft, false});
    assert_text_absent(report, &layout, "1234 delivered");
}

static void initialise_gives_a_disconnected_unlocked_empty_state(RemoteTestReport* report) {
    RemoteDisplayState display_state;
    memset(&display_state, 0x5A, sizeof(display_state));
    remote_display_state_initialise(&display_state);
    REMOTE_TEST_ASSERT(report, !display_state.link_connected, "not connected");
    REMOTE_TEST_ASSERT(report, !display_state.link_incompatible, "not incompatible");
    REMOTE_TEST_ASSERT(report, !display_state.link_connecting, "not connecting");
    REMOTE_TEST_ASSERT(report, !display_state.screen_locked, "unlocked");
    REMOTE_TEST_ASSERT(report, !display_state.nfc_presenting, "not presenting");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteDisplayStatusReady, display_state.status, "ready");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, RemoteDisplayPageNone, display_state.page, "no page");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 0, display_state.delivered_count, "nothing delivered");
    REMOTE_TEST_ASSERT(report, display_state.payload[0] == '\0', "empty payload");
    REMOTE_TEST_ASSERT(report, display_state.error_code[0] == '\0', "no error");
}

int main(void) {
    static const RemoteTestCase test_cases[] = {
        {"every fixture composes within bounds with a header", every_fixture_composes_within_bounds_with_a_header},
        {"connecting shows a distinct screen", connecting_shows_a_distinct_screen},
        {"an incompatible link shows a distinct screen", an_incompatible_link_shows_a_distinct_screen},
        {"not connected overrides whatever the record says", not_connected_overrides_whatever_the_record_says},
        {"ready shows the word and the new session hint", ready_shows_the_word_and_the_new_session_hint},
        {"presenting wifi places the code and the column", presenting_wifi_places_the_code_and_the_column},
        {"presenting guest is page two titled gallery", presenting_guest_is_page_two_titled_gallery},
        {"guest connected keeps the code and changes the status line", guest_connected_keeps_the_code_and_changes_the_status_line},
        {"terminating and recovery are two centred lines without a code", terminating_and_recovery_are_two_centred_lines_without_a_code},
        {"the status decides the screen not the page", the_status_decides_the_screen_not_the_page},
        {"an active status with no page falls back to a centred status", an_active_status_with_no_page_falls_back_to_a_centred_status},
        {"a page with an empty payload still frames the code area", a_page_with_an_empty_payload_still_frames_the_code_area},
        {"an error is an inverted band at the bottom", an_error_is_an_inverted_band_at_the_bottom},
        {"no error means no band", no_error_means_no_band},
        {"locked inverts the full header on a screen without a code", locked_inverts_the_full_header_on_a_screen_without_a_code},
        {"locked inverts only the column header beside a code", locked_inverts_only_the_column_header_beside_a_code},
        {"the nfc marker shows in the column header only while presenting", the_nfc_marker_shows_in_the_column_header_only_while_presenting},
        {"an unknown status renders a safe fallback", an_unknown_status_renders_a_safe_fallback},
        {"an unknown page is treated as no page", an_unknown_page_is_treated_as_no_page},
        {"long values truncate rather than overflow", long_values_truncate_rather_than_overflow},
        {"many delivered is capped so it fits the column", many_delivered_is_capped_so_it_fits_the_column},
        {"initialise gives a disconnected unlocked empty state", initialise_gives_a_disconnected_unlocked_empty_state},
    };
    return remote_test_run_all(test_cases, REMOTE_TEST_ROW_COUNT(test_cases));
}
