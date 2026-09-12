/*
 * The display layout: what goes where on the 128 by 64 screen for a given
 * display state.
 *
 * This module has no SDK dependency so that every screen can be composed and
 * checked on a development machine from a fixture (specification FE2, Done
 * when). The application glue replays the composed layout onto the canvas and
 * decides nothing about placement itself.
 *
 * The state is the one record the appliance will send (extension 3.2:
 * status, page, payload, delivered count, error code) plus the two facts only
 * the device knows: whether the link is up and whether the screen is locked.
 * The device renders the last record received and holds no belief of its own
 * about any of it (specification 2.5).
 *
 * Positions are the layouts the author accepted as FD3 on 2026-09-11 and are
 * expressed once, here. Font metrics used for truncation come from the
 * firmware at the pinned commit: Primary is 8 pixels high, Secondary 7.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Status codes fixed by extension 3.2. Their wire spelling is FE3's; the
 * order here is not a contract. */
typedef enum {
    RemoteDisplayStatusReady,
    RemoteDisplayStatusPresenting,
    RemoteDisplayStatusGuestConnected,
    RemoteDisplayStatusTerminating,
    RemoteDisplayStatusRecoveryRequired,
    RemoteDisplayStatusCount,
} RemoteDisplayStatus;

typedef enum {
    RemoteDisplayPageNone,
    RemoteDisplayPageWifi,
    RemoteDisplayPageGuest,
    RemoteDisplayPageCount,
} RemoteDisplayPage;

/*
 * Provisional bounds. The payload bound is FD9 and the error code is FD8,
 * both settled at FE3 and frozen at FD20; until then these are sized from the
 * seam document's proposal (256 bytes of payload) so a Wi-Fi payload with a
 * full length SSID and passphrase is never refused by the display before the
 * QR encoder has had a chance to say whether it fits. Sized once, at compile
 * time, so nothing allocates in the input path (specification 0.10).
 */
#define REMOTE_DISPLAY_PAYLOAD_CAPACITY    257
#define REMOTE_DISPLAY_ERROR_CODE_CAPACITY 33

typedef struct {
    /* Plain integers rather than the enums so an out of range value received
     * from the appliance can be rendered as a safe fallback rather than
     * invoking undefined behaviour. */
    int status;
    int page;
    char payload[REMOTE_DISPLAY_PAYLOAD_CAPACITY];
    unsigned int delivered_count;
    /* Empty when there is no error. Rendered as the code itself, so nothing
     * here has to be kept in step with the code set the appliance defines. */
    char error_code[REMOTE_DISPLAY_ERROR_CODE_CAPACITY];
    bool link_connected;
    /* Local link facts, shown instead of the appliance's record when set.
     * Both default false, so a state that only sets link_connected behaves
     * exactly as before these were added (FE2). Incompatible wins over
     * connecting, which wins over a plain disconnection. */
    bool link_incompatible;
    bool link_connecting;
    bool screen_locked;
    /* Whether the NFC surface is presenting this page's record. Local, like
     * the lock: the appliance never sends it. */
    bool nfc_presenting;
} RemoteDisplayState;

/* Sets every field to the not connected, unlocked, empty state. */
void remote_display_state_initialise(RemoteDisplayState* display_state);

/* The screen geometry and the FD3 positions. */
#define REMOTE_DISPLAY_WIDTH  128
#define REMOTE_DISPLAY_HEIGHT 64

#define REMOTE_LAYOUT_HEADER_HEIGHT   12
#define REMOTE_LAYOUT_HEADER_BASELINE 9
#define REMOTE_LAYOUT_QR_X            0
#define REMOTE_LAYOUT_QR_Y            3
#define REMOTE_LAYOUT_QR_SIZE         58
#define REMOTE_LAYOUT_COLUMN_X        62
#define REMOTE_LAYOUT_COLUMN_WIDTH    66
#define REMOTE_LAYOUT_ERROR_BAND_Y    52
#define REMOTE_LAYOUT_ERROR_BAND_HEIGHT 12

typedef enum {
    RemoteLayoutFontPrimary,
    RemoteLayoutFontSecondary,
} RemoteLayoutFont;

typedef enum {
    RemoteLayoutAnchorLeft,
    RemoteLayoutAnchorCenter,
    RemoteLayoutAnchorRight,
} RemoteLayoutAnchor;

/* Long enough for any label this module writes plus the longest bounded
 * error code; longer input is truncated, never overflowed. */
#define REMOTE_LAYOUT_TEXT_CAPACITY 40

typedef struct {
    int x;
    /* Text baseline. */
    int y;
    RemoteLayoutFont font;
    RemoteLayoutAnchor anchor;
    /* Drawn in the background colour, over a filled box. */
    bool inverted;
    /* The width the text was truncated to fit. The draw glue may clip
     * further using the real glyph widths; it never widens. */
    int max_width_pixels;
    char text[REMOTE_LAYOUT_TEXT_CAPACITY];
} RemoteLayoutText;

typedef enum {
    RemoteLayoutShapeFilledBox,
    RemoteLayoutShapeFrame,
} RemoteLayoutShapeKind;

typedef struct {
    RemoteLayoutShapeKind kind;
    int x;
    int y;
    int width;
    int height;
} RemoteLayoutShape;

#define REMOTE_LAYOUT_MAX_SHAPES 6
#define REMOTE_LAYOUT_MAX_TEXTS  8

typedef struct {
    /* Drawn first, in order, then the texts, so inverted text lands on its box. */
    RemoteLayoutShape shapes[REMOTE_LAYOUT_MAX_SHAPES];
    int shape_count;
    RemoteLayoutText texts[REMOTE_LAYOUT_MAX_TEXTS];
    int text_count;
    /* The firmware's own centre button hint, drawn at the bottom by the glue. */
    bool center_button_shown;
    char center_button_label[REMOTE_LAYOUT_TEXT_CAPACITY];
    /* Where the QR belongs when a page is showing. FE5 renders the matrix
     * here; until then the glue draws the frame and the page name inside. */
    bool qr_area_shown;
    int qr_x;
    int qr_y;
    int qr_size;
    /* True whenever this is an active code page (a Wi-Fi or gallery page the
     * appliance is presenting), whether or not the payload fits the QR. The
     * glue presents the NFC record on this, not on qr_area_shown, so a payload
     * too large for the version 3 QR still reaches a phone by a tap (FE5, FE6):
     * NDEF has no such small ceiling. */
    bool code_page_active;
} RemoteDisplayLayout;

/* Composes the whole screen for one state. Never fails: an unrecognised
 * status or page renders a fallback screen rather than nothing. */
void remote_display_layout_compose(const RemoteDisplayState* display_state, RemoteDisplayLayout* layout);

/* Finds the first text placement whose content equals the given string, or
 * NULL. For tests and for the glue; not a rendering operation. */
const RemoteLayoutText* remote_display_layout_find_text(const RemoteDisplayLayout* layout, const char* text);

#ifdef __cplusplus
}
#endif
