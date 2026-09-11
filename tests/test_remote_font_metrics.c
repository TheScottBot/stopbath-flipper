/*
 * FE2: the host side text measurement agrees with the firmware fonts. The
 * expected widths were decoded from the pinned firmware's u8g2 font data on
 * 2026-09-11 (recorded in the evaluation log 4.4) and pin the generated
 * table to that firmware.
 */
#include "test_support.h"

#include "../remote_display/remote_font_metrics.h"

typedef struct {
    RemoteLayoutFont font;
    const char* text;
    int expected_width;
} MeasuredWidth;

static const MeasuredWidth measured_widths[] = {
    {RemoteLayoutFontPrimary, "StopBath", 46},
    {RemoteLayoutFontPrimary, "Recovery needed", 85},
    {RemoteLayoutFontPrimary, "W", 11},
    {RemoteLayoutFontSecondary, "StopBath", 40},
    {RemoteLayoutFontSecondary, "999+ delivered", 64},
    {RemoteLayoutFontSecondary, "TERMINATION_NOT_PERMITTED", 142},
    {RemoteLayoutFontSecondary, "", 0},
};

static void widths_match_the_firmware_fonts(RemoteTestReport* report) {
    for(int row_index = 0; row_index < REMOTE_TEST_ROW_COUNT(measured_widths); row_index++) {
        const MeasuredWidth* row = &measured_widths[row_index];
        int width = remote_font_text_width(row->font, row->text, strlen(row->text));
        REMOTE_TEST_ASSERT_EQUAL_INT(report, row->expected_width, width, row->text);
    }
}

static void characters_outside_printable_ascii_have_no_width(RemoteTestReport* report) {
    const char with_control[] = {'A', '\t', 'B', '\0'};
    const char with_high_bit[] = {'A', (char)0xC3, (char)0xA9, 'B', '\0'};
    int plain = remote_font_text_width(RemoteLayoutFontPrimary, "AB", 2);
    REMOTE_TEST_ASSERT_EQUAL_INT(report, plain, remote_font_text_width(RemoteLayoutFontPrimary, with_control, 3), "control character");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, plain, remote_font_text_width(RemoteLayoutFontPrimary, with_high_bit, 4), "non ASCII bytes");
}

static void fit_prefix_keeps_the_longest_prefix_that_fits(RemoteTestReport* report) {
    /* "999+ delivered" is 64 pixels in Secondary. */
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 14, remote_font_fit_prefix_length(RemoteLayoutFontSecondary, "999+ delivered", 66), "fits exactly within the column");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 14, remote_font_fit_prefix_length(RemoteLayoutFontSecondary, "999+ delivered", 64), "fits at exactly its width");
    size_t at_63 = remote_font_fit_prefix_length(RemoteLayoutFontSecondary, "999+ delivered", 63);
    REMOTE_TEST_ASSERT(report, at_63 < 14, "one pixel short drops a character");
    REMOTE_TEST_ASSERT(report, remote_font_text_width(RemoteLayoutFontSecondary, "999+ delivered", at_63) <= 63, "what is kept fits");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 0, remote_font_fit_prefix_length(RemoteLayoutFontPrimary, "W", 10), "nothing fits when the first glyph does not");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 0, remote_font_fit_prefix_length(RemoteLayoutFontPrimary, "", 100), "empty text");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 0, remote_font_fit_prefix_length(RemoteLayoutFontPrimary, "abc", 0), "zero width");
}

int main(void) {
    static const RemoteTestCase test_cases[] = {
        {"widths match the firmware fonts", widths_match_the_firmware_fonts},
        {"characters outside printable ascii have no width", characters_outside_printable_ascii_have_no_width},
        {"fit prefix keeps the longest prefix that fits", fit_prefix_keeps_the_longest_prefix_that_fits},
    };
    return remote_test_run_all(test_cases, REMOTE_TEST_ROW_COUNT(test_cases));
}
