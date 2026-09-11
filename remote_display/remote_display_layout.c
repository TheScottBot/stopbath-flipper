#include "remote_display_layout.h"

#include <stdio.h>
#include <string.h>

#include "remote_font_metrics.h"

/* FD3 positions not already named in the header. */
#define CENTRED_X                 (REMOTE_DISPLAY_WIDTH / 2)
#define CENTRED_PRIMARY_BASELINE  34
#define CENTRED_SECONDARY_BASELINE 50
#define COLUMN_TITLE_BASELINE     23
#define COLUMN_PAGE_BASELINE      34
#define COLUMN_STATUS_BASELINE    45
#define COLUMN_DELIVERED_BASELINE 56
#define HEADER_RIGHT_X            (REMOTE_DISPLAY_WIDTH - 2)
#define ERROR_BAND_BASELINE       61
/* Beside a code, bands start two pixels left of the text column so the
 * text keeps its margin, and never reach the code area. */
#define COLUMN_BAND_X             (REMOTE_LAYOUT_COLUMN_X - 2)
#define COLUMN_BAND_WIDTH         (REMOTE_DISPLAY_WIDTH - COLUMN_BAND_X)

/* Four digits do not fit the column beside the word, and a session lasts
 * minutes, so the count is capped for display rather than truncated mid
 * number. */
#define DELIVERED_DISPLAY_CAP 999

void remote_display_state_initialise(RemoteDisplayState* display_state) {
    memset(display_state, 0, sizeof(*display_state));
    display_state->status = RemoteDisplayStatusReady;
    display_state->page = RemoteDisplayPageNone;
    display_state->link_connected = false;
    display_state->screen_locked = false;
}

static void add_shape(RemoteDisplayLayout* layout, RemoteLayoutShapeKind kind, int x, int y, int width, int height) {
    if(layout->shape_count >= REMOTE_LAYOUT_MAX_SHAPES) {
        /* The screens are fixed and never reach the limit; if one ever does,
         * dropping a shape is safer than writing past the array. */
        return;
    }
    RemoteLayoutShape* shape = &layout->shapes[layout->shape_count++];
    shape->kind = kind;
    shape->x = x;
    shape->y = y;
    shape->width = width;
    shape->height = height;
}

/* Places text, truncated to the narrower of the buffer and the width it has
 * been given, measured with the firmware's own glyph advances so the host
 * and the device agree on what fits. Truncation keeps the start of the
 * string, which is where a code or a label carries its meaning. */
static void add_text(
    RemoteDisplayLayout* layout,
    int x,
    int y,
    RemoteLayoutFont font,
    RemoteLayoutAnchor anchor,
    bool inverted,
    int max_width_pixels,
    const char* text) {
    if(layout->text_count >= REMOTE_LAYOUT_MAX_TEXTS) {
        return;
    }
    RemoteLayoutText* placed = &layout->texts[layout->text_count++];
    placed->x = x;
    placed->y = y;
    placed->font = font;
    placed->anchor = anchor;
    placed->inverted = inverted;
    placed->max_width_pixels = max_width_pixels;

    size_t copy_length = remote_font_fit_prefix_length(font, text, max_width_pixels);
    if(copy_length > REMOTE_LAYOUT_TEXT_CAPACITY - 1) copy_length = REMOTE_LAYOUT_TEXT_CAPACITY - 1;
    memcpy(placed->text, text, copy_length);
    placed->text[copy_length] = '\0';
}

/* The header on a screen with no code: full width, the product name, and
 * when locked an inverted band with the word at the right (specification
 * 2.4: the locked state must be obvious at a glance). */
static void compose_full_width_header(RemoteDisplayLayout* layout, bool screen_locked) {
    if(screen_locked) {
        add_shape(layout, RemoteLayoutShapeFilledBox, 0, 0, REMOTE_DISPLAY_WIDTH, REMOTE_LAYOUT_HEADER_HEIGHT);
    }
    add_text(layout, 2, REMOTE_LAYOUT_HEADER_BASELINE, RemoteLayoutFontPrimary, RemoteLayoutAnchorLeft, screen_locked, 70, "StopBath");
    if(screen_locked) {
        add_text(layout, HEADER_RIGHT_X, REMOTE_LAYOUT_HEADER_BASELINE, RemoteLayoutFontPrimary, RemoteLayoutAnchorRight, true, 50, "LOCKED");
    }
}

/* The header beside a code: confined to the column, because anything drawn
 * across the code makes it unscannable, and the person scanning is not the
 * person holding the device. The column is too narrow for both words, so
 * when locked the product name gives way to the one that matters. */
static void compose_column_header(RemoteDisplayLayout* layout, bool screen_locked, bool nfc_presenting) {
    if(screen_locked) {
        add_shape(layout, RemoteLayoutShapeFilledBox, COLUMN_BAND_X, 0, COLUMN_BAND_WIDTH, REMOTE_LAYOUT_HEADER_HEIGHT);
        add_text(layout, REMOTE_LAYOUT_COLUMN_X, REMOTE_LAYOUT_HEADER_BASELINE, RemoteLayoutFontPrimary, RemoteLayoutAnchorLeft, true, REMOTE_LAYOUT_COLUMN_WIDTH, "LOCKED");
    } else {
        add_text(layout, REMOTE_LAYOUT_COLUMN_X, REMOTE_LAYOUT_HEADER_BASELINE, RemoteLayoutFontPrimary, RemoteLayoutAnchorLeft, false, REMOTE_LAYOUT_COLUMN_WIDTH, "StopBath");
    }
    /* A small marker at the right of the header row while the NFC surface is
     * live, so the photographer can tell a tap will do something. The
     * longer header word ends at 108 pixels and the marker, 18 pixels wide, starts at 108; the two touch only when locked, where the band carries both. */
    if(nfc_presenting) {
        add_text(layout, HEADER_RIGHT_X, REMOTE_LAYOUT_HEADER_BASELINE, RemoteLayoutFontSecondary, RemoteLayoutAnchorRight, screen_locked, 24, "NFC");
    }
}

static void compose_two_centred_lines(RemoteDisplayLayout* layout, const char* primary_line, const char* secondary_line) {
    add_text(layout, CENTRED_X, CENTRED_PRIMARY_BASELINE, RemoteLayoutFontPrimary, RemoteLayoutAnchorCenter, false, REMOTE_DISPLAY_WIDTH - 4, primary_line);
    if(secondary_line != NULL) {
        add_text(layout, CENTRED_X, CENTRED_SECONDARY_BASELINE, RemoteLayoutFontSecondary, RemoteLayoutAnchorCenter, false, REMOTE_DISPLAY_WIDTH - 4, secondary_line);
    }
}

static const char* status_line_for(RemoteDisplayStatus status) {
    return status == RemoteDisplayStatusGuestConnected ? "Guest joined" : "Presenting";
}

static void compose_code_page(RemoteDisplayLayout* layout, const RemoteDisplayState* display_state, RemoteDisplayStatus status, RemoteDisplayPage page, bool error_band_shown) {
    layout->qr_area_shown = true;
    layout->qr_x = REMOTE_LAYOUT_QR_X;
    layout->qr_y = REMOTE_LAYOUT_QR_Y;
    layout->qr_size = REMOTE_LAYOUT_QR_SIZE;

    /* Wi-Fi first, gallery second, in the order specification 2.7 fixes. */
    const char* page_title = page == RemoteDisplayPageWifi ? "Wi-Fi" : "Gallery";
    const char* page_indicator = page == RemoteDisplayPageWifi ? "<  1/2  >" : "<  2/2  >";

    add_text(layout, REMOTE_LAYOUT_COLUMN_X, COLUMN_TITLE_BASELINE, RemoteLayoutFontPrimary, RemoteLayoutAnchorLeft, false, REMOTE_LAYOUT_COLUMN_WIDTH, page_title);
    add_text(layout, REMOTE_LAYOUT_COLUMN_X, COLUMN_PAGE_BASELINE, RemoteLayoutFontSecondary, RemoteLayoutAnchorLeft, false, REMOTE_LAYOUT_COLUMN_WIDTH, page_indicator);
    add_text(layout, REMOTE_LAYOUT_COLUMN_X, COLUMN_STATUS_BASELINE, RemoteLayoutFontSecondary, RemoteLayoutAnchorLeft, false, REMOTE_LAYOUT_COLUMN_WIDTH, status_line_for(status));

    /* The delivered line shares its row with the error band, and the band
     * matters more when it is there. Zero is not worth a line. */
    if(display_state->delivered_count > 0 && !error_band_shown) {
        char delivered_line[REMOTE_LAYOUT_TEXT_CAPACITY];
        if(display_state->delivered_count > DELIVERED_DISPLAY_CAP) {
            snprintf(delivered_line, sizeof(delivered_line), "%d+ delivered", DELIVERED_DISPLAY_CAP);
        } else {
            snprintf(delivered_line, sizeof(delivered_line), "%u delivered", display_state->delivered_count);
        }
        add_text(layout, REMOTE_LAYOUT_COLUMN_X, COLUMN_DELIVERED_BASELINE, RemoteLayoutFontSecondary, RemoteLayoutAnchorLeft, false, REMOTE_LAYOUT_COLUMN_WIDTH, delivered_line);
    }
}

/* The error code itself, not a sentence: the appliance owns meaning and this
 * device owns presentation (specification 2.6). Full width on a screen with
 * no code; confined to the column beside one, in the delivered line's row. */
static void compose_error_band(RemoteDisplayLayout* layout, const char* error_code, bool beside_a_code) {
    if(beside_a_code) {
        add_shape(layout, RemoteLayoutShapeFilledBox, COLUMN_BAND_X, REMOTE_LAYOUT_ERROR_BAND_Y, COLUMN_BAND_WIDTH, REMOTE_LAYOUT_ERROR_BAND_HEIGHT);
        add_text(layout, REMOTE_LAYOUT_COLUMN_X, ERROR_BAND_BASELINE, RemoteLayoutFontSecondary, RemoteLayoutAnchorLeft, true, REMOTE_LAYOUT_COLUMN_WIDTH, error_code);
    } else {
        add_shape(layout, RemoteLayoutShapeFilledBox, 0, REMOTE_LAYOUT_ERROR_BAND_Y, REMOTE_DISPLAY_WIDTH, REMOTE_LAYOUT_ERROR_BAND_HEIGHT);
        add_text(layout, CENTRED_X, ERROR_BAND_BASELINE, RemoteLayoutFontSecondary, RemoteLayoutAnchorCenter, true, REMOTE_DISPLAY_WIDTH - 4, error_code);
    }
}

/* Whether this state puts a code on the screen, which decides where the
 * header and the error band may go. A page is meaningful only while active
 * (extension 3.2), and only a known page names something to encode. */
static bool state_shows_a_code(const RemoteDisplayState* display_state) {
    if(!display_state->link_connected) return false;
    bool active = display_state->status == RemoteDisplayStatusPresenting ||
                  display_state->status == RemoteDisplayStatusGuestConnected;
    bool page_named = display_state->page == RemoteDisplayPageWifi ||
                      display_state->page == RemoteDisplayPageGuest;
    return active && page_named;
}

void remote_display_layout_compose(const RemoteDisplayState* display_state, RemoteDisplayLayout* layout) {
    memset(layout, 0, sizeof(*layout));
    bool beside_a_code = state_shows_a_code(display_state);
    if(beside_a_code) {
        compose_column_header(layout, display_state->screen_locked, display_state->nfc_presenting);
    } else {
        compose_full_width_header(layout, display_state->screen_locked);
    }

    /* Stale content is worse than none: the device cannot know whether what
     * it holds is still true once the link is down (specification 2.5). The
     * local link facts are shown in place of any record, most specific first. */
    if(display_state->link_incompatible) {
        /* A version the appliance will not talk to (2.11: faults visible). */
        compose_two_centred_lines(layout, "Incompatible", "Update remote");
        return;
    }
    if(!display_state->link_connected) {
        if(display_state->link_connecting) {
            compose_two_centred_lines(layout, "Connecting", "Please wait");
        } else {
            compose_two_centred_lines(layout, "Pi disconnected", "Reconnect USB");
        }
        return;
    }

    bool status_known = display_state->status >= 0 && display_state->status < (int)RemoteDisplayStatusCount;
    bool page_known = display_state->page >= 0 && display_state->page < (int)RemoteDisplayPageCount;
    RemoteDisplayPage page = page_known ? (RemoteDisplayPage)display_state->page : RemoteDisplayPageNone;
    bool error_band_shown = display_state->error_code[0] != '\0';

    if(!status_known) {
        /* A code this build does not know, from a newer appliance. Something
         * legible rather than blank or garbage. */
        compose_two_centred_lines(layout, "Unknown status", NULL);
    } else {
        RemoteDisplayStatus status = (RemoteDisplayStatus)display_state->status;
        switch(status) {
        case RemoteDisplayStatusReady:
            compose_two_centred_lines(layout, "Ready", NULL);
            if(!error_band_shown) {
                layout->center_button_shown = true;
                snprintf(layout->center_button_label, sizeof(layout->center_button_label), "%s", "New session");
            }
            break;
        case RemoteDisplayStatusPresenting:
        case RemoteDisplayStatusGuestConnected:
            /* A page is meaningful only while active (extension 3.2); with
             * none named there is nothing to encode, so the status stands
             * alone. */
            if(page == RemoteDisplayPageNone) {
                compose_two_centred_lines(layout, status_line_for(status), NULL);
            } else {
                compose_code_page(layout, display_state, status, page, error_band_shown);
            }
            break;
        case RemoteDisplayStatusTerminating:
            compose_two_centred_lines(layout, "Ending session", "Please wait");
            break;
        case RemoteDisplayStatusRecoveryRequired:
            compose_two_centred_lines(layout, "Recovery needed", "See dashboard");
            break;
        case RemoteDisplayStatusCount:
            break;
        }
    }

    if(error_band_shown) {
        compose_error_band(layout, display_state->error_code, beside_a_code);
    }
}

const RemoteLayoutText* remote_display_layout_find_text(const RemoteDisplayLayout* layout, const char* text) {
    for(int text_index = 0; text_index < layout->text_count; text_index++) {
        if(strcmp(layout->texts[text_index].text, text) == 0) {
            return &layout->texts[text_index];
        }
    }
    return NULL;
}
