/*
 * QR encoding for the display, brought forward from FE5 for the FD4
 * experiment: a known payload produces a symbol of the known version with
 * the finder patterns where the standard puts them; a payload too long for
 * the display ceiling is refused, not truncated; the rendered bitmap places
 * modules at two pixels each, centred.
 *
 * Matching against published full matrix vectors is FE5's own criterion and
 * is not claimed here.
 */
#include "test_support.h"

#include "../remote_display/remote_display_fixtures.h"
#include "../remote_display/remote_qr.h"

/* A Wi-Fi payload of the experiment's shape with the example credentials:
 * 41 bytes, byte mode, version 3. The real experiment credentials live in an
 * untracked header and are exercised by the shared fixtures test below. */
#define WIFI_EXPERIMENT_PAYLOAD "WIFI:T:WPA;S:StopBathExample;P:example1;;"
/* The bare local gallery address per D30, upper case so it qualifies for
 * alphanumeric mode: 20 characters, version 1. */
#define GUEST_ADDRESS_PAYLOAD "HTTP://192.168.72.1/"

/* A 7 by 7 finder pattern: dark ring, light ring, dark 3 by 3 centre. */
static bool finder_pattern_at(const RemoteQrMatrix* matrix, int origin_x, int origin_y) {
    for(int y = 0; y < 7; y++) {
        for(int x = 0; x < 7; x++) {
            bool on_outer_ring = x == 0 || x == 6 || y == 0 || y == 6;
            bool in_centre = x >= 2 && x <= 4 && y >= 2 && y <= 4;
            bool expected_dark = on_outer_ring || in_centre;
            if(remote_qr_module_is_dark(matrix, origin_x + x, origin_y + y) != expected_dark) {
                return false;
            }
        }
    }
    return true;
}

static void the_wifi_experiment_payload_is_a_version_three_symbol(RemoteTestReport* report) {
    RemoteQrMatrix matrix;
    REMOTE_TEST_ASSERT(report, remote_qr_encode(WIFI_EXPERIMENT_PAYLOAD, &matrix), "encodes");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 29, matrix.side, "version 3 is 29 modules");
    REMOTE_TEST_ASSERT(report, finder_pattern_at(&matrix, 0, 0), "top left finder");
    REMOTE_TEST_ASSERT(report, finder_pattern_at(&matrix, matrix.side - 7, 0), "top right finder");
    REMOTE_TEST_ASSERT(report, finder_pattern_at(&matrix, 0, matrix.side - 7), "bottom left finder");
}

static void the_gallery_address_is_a_version_one_symbol(RemoteTestReport* report) {
    RemoteQrMatrix matrix;
    REMOTE_TEST_ASSERT(report, remote_qr_encode(GUEST_ADDRESS_PAYLOAD, &matrix), "encodes");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 21, matrix.side, "version 1 is 21 modules");
    REMOTE_TEST_ASSERT(report, finder_pattern_at(&matrix, 0, 0), "top left finder");
    REMOTE_TEST_ASSERT(report, finder_pattern_at(&matrix, 14, 0), "top right finder");
    REMOTE_TEST_ASSERT(report, finder_pattern_at(&matrix, 0, 14), "bottom left finder");
}

static void the_shared_fixtures_encode(RemoteTestReport* report) {
    /* Whatever the fixtures carry must render, or the carousel shows a frame
     * where the author expects a code. */
    const RemoteDisplayFixture* fixtures = remote_display_fixtures();
    for(int fixture_index = 0; fixture_index < remote_display_fixture_count(); fixture_index++) {
        const RemoteDisplayState* display_state = &fixtures[fixture_index].display_state;
        if(display_state->payload[0] == '\0') continue;
        RemoteQrMatrix matrix;
        REMOTE_TEST_ASSERT(report, remote_qr_encode(display_state->payload, &matrix), fixtures[fixture_index].fixture_name);
    }
}

static void a_payload_beyond_the_display_ceiling_is_refused(RemoteTestReport* report) {
    /* Version 3 at the lowest error correction holds 53 bytes in byte mode
     * (ISO/IEC 18004 capacity table, as the library implements it). Exactly
     * at the bound encodes; one over is refused, not truncated. */
    char at_bound[54];
    memset(at_bound, 'x', 53);
    at_bound[53] = '\0';
    char one_over[55];
    memset(one_over, 'x', 54);
    one_over[54] = '\0';

    RemoteQrMatrix matrix;
    REMOTE_TEST_ASSERT(report, remote_qr_encode(at_bound, &matrix), "53 bytes encodes");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 29, matrix.side, "at version 3");
    REMOTE_TEST_ASSERT(report, !remote_qr_encode(one_over, &matrix), "54 bytes refused");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 0, matrix.side, "refusal leaves the matrix empty");
}

static void an_empty_payload_is_refused(RemoteTestReport* report) {
    RemoteQrMatrix matrix;
    REMOTE_TEST_ASSERT(report, !remote_qr_encode("", &matrix), "nothing to encode");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 0, matrix.side, "empty");
}

static void module_queries_outside_the_symbol_are_light(RemoteTestReport* report) {
    RemoteQrMatrix matrix;
    remote_qr_encode(GUEST_ADDRESS_PAYLOAD, &matrix);
    REMOTE_TEST_ASSERT(report, !remote_qr_module_is_dark(&matrix, -1, 0), "left of the symbol");
    REMOTE_TEST_ASSERT(report, !remote_qr_module_is_dark(&matrix, 21, 0), "right of the symbol");
    REMOTE_TEST_ASSERT(report, !remote_qr_module_is_dark(&matrix, 0, 1000), "far below");
    RemoteQrMatrix empty = {0};
    REMOTE_TEST_ASSERT(report, !remote_qr_module_is_dark(&empty, 0, 0), "an empty matrix has no dark module");
}

static void the_bitmap_draws_each_module_as_two_by_two_centred(RemoteTestReport* report) {
    RemoteQrMatrix matrix;
    remote_qr_encode(GUEST_ADDRESS_PAYLOAD, &matrix);
    RemoteQrBitmap bitmap;
    remote_qr_render_bitmap(&matrix, &bitmap);

    /* 21 modules at 2 pixels is 42 pixels inside 58, so the symbol starts
     * at pixel 8, leaving a 4 module quiet zone on every side. */
    int origin = (REMOTE_LAYOUT_QR_SIZE - 21 * REMOTE_QR_PIXELS_PER_MODULE) / 2;
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 8, origin, "version 1 origin");
    for(int y = 0; y < REMOTE_LAYOUT_QR_SIZE; y++) {
        for(int x = 0; x < REMOTE_LAYOUT_QR_SIZE; x++) {
            int module_x = (x - origin) / REMOTE_QR_PIXELS_PER_MODULE;
            int module_y = (y - origin) / REMOTE_QR_PIXELS_PER_MODULE;
            bool inside = x >= origin && y >= origin && module_x < 21 && module_y < 21;
            bool expected = inside && remote_qr_module_is_dark(&matrix, module_x, module_y);
            if(remote_qr_bitmap_pixel_is_dark(&bitmap, x, y) != expected) {
                REMOTE_TEST_ASSERT(report, false, "pixel disagrees with its module");
                return;
            }
        }
    }
    REMOTE_TEST_ASSERT(report, true, "every pixel agrees with its module");
}

static void a_version_three_symbol_fills_the_area_exactly(RemoteTestReport* report) {
    RemoteQrMatrix matrix;
    remote_qr_encode(WIFI_EXPERIMENT_PAYLOAD, &matrix);
    RemoteQrBitmap bitmap;
    remote_qr_render_bitmap(&matrix, &bitmap);
    /* 29 modules at 2 pixels is 58: origin 0, top left finder's outer ring
     * is dark at the very first pixel. */
    REMOTE_TEST_ASSERT(report, remote_qr_bitmap_pixel_is_dark(&bitmap, 0, 0), "first pixel dark");
    REMOTE_TEST_ASSERT(report, remote_qr_bitmap_pixel_is_dark(&bitmap, 1, 1), "two pixels per module");
    REMOTE_TEST_ASSERT(report, !remote_qr_bitmap_pixel_is_dark(&bitmap, 2, 2), "module one one is light in a finder");
    REMOTE_TEST_ASSERT(report, remote_qr_bitmap_pixel_is_dark(&bitmap, 57, 0), "last column is the top right finder");
}

static void an_empty_matrix_renders_a_blank_bitmap(RemoteTestReport* report) {
    RemoteQrMatrix empty = {0};
    RemoteQrBitmap bitmap;
    memset(&bitmap, 0xFF, sizeof(bitmap));
    remote_qr_render_bitmap(&empty, &bitmap);
    bool any_dark = false;
    for(size_t byte_index = 0; byte_index < sizeof(bitmap.bits); byte_index++) {
        if(bitmap.bits[byte_index] != 0) any_dark = true;
    }
    REMOTE_TEST_ASSERT(report, !any_dark, "blank");
}

int main(void) {
    static const RemoteTestCase test_cases[] = {
        {"the wifi experiment payload is a version three symbol", the_wifi_experiment_payload_is_a_version_three_symbol},
        {"the gallery address is a version one symbol", the_gallery_address_is_a_version_one_symbol},
        {"the shared fixtures encode", the_shared_fixtures_encode},
        {"a payload beyond the display ceiling is refused", a_payload_beyond_the_display_ceiling_is_refused},
        {"an empty payload is refused", an_empty_payload_is_refused},
        {"module queries outside the symbol are light", module_queries_outside_the_symbol_are_light},
        {"the bitmap draws each module as two by two centred", the_bitmap_draws_each_module_as_two_by_two_centred},
        {"a version three symbol fills the area exactly", a_version_three_symbol_fills_the_area_exactly},
        {"an empty matrix renders a blank bitmap", an_empty_matrix_renders_a_blank_bitmap},
    };
    return remote_test_run_all(test_cases, REMOTE_TEST_ROW_COUNT(test_cases));
}
