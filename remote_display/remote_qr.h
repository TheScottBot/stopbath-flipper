/*
 * QR encoding for the code area of the display, on top of the vendored
 * qrcodegen library (lib/qrcodegen, see IMPLEMENTATION_DEVIATIONS.md and the
 * dependency record in docs/evaluation/ACTUAL_CONTRACT_EVALUATION.md).
 *
 * No SDK dependency, no allocation: every buffer is sized at compile time
 * from the largest version this display can show, so a payload that would
 * need more is refused rather than truncated or drawn unscannable
 * (specification FE5).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "remote_display_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The code area is 58 pixels (FD3). At two pixels per module that is 29
 * modules, which is version 3. Version 4 is 33 modules and does not fit at
 * two pixels, and one pixel per module is not a scanning candidate. So
 * version 3 is the ceiling this display imposes, not a preference. */
#define REMOTE_QR_MAX_VERSION       3
#define REMOTE_QR_MAX_SIDE          (REMOTE_QR_MAX_VERSION * 4 + 17)
#define REMOTE_QR_PIXELS_PER_MODULE 2

/* The library's own sizing for a version 3 symbol: ceil(29 * 29 / 8) + 1. */
#define REMOTE_QR_BUFFER_LENGTH (((REMOTE_QR_MAX_SIDE * REMOTE_QR_MAX_SIDE) + 7) / 8 + 1)

typedef struct {
    /* Modules per side, 21 to 29; zero when nothing is encoded. */
    int side;
    uint8_t encoded[REMOTE_QR_BUFFER_LENGTH];
} RemoteQrMatrix;

/* Encodes the payload at the smallest version that holds it, up to the
 * ceiling. Returns false, leaving the matrix empty, when the payload needs
 * a larger version or is empty. Error correction starts at the lowest level
 * so that capacity is not spent before it has to be, and is raised by the
 * library where a higher level fits in the same version. */
bool remote_qr_encode(const char* payload, RemoteQrMatrix* matrix);

/* Whether remote_qr_encode would succeed for this payload, without keeping the
 * matrix. The layout uses it to decide, before it composes, whether a code page
 * can show its code or must show a distinct error in its place (FE5: an
 * oversized payload is refused with a distinct error rather than rendering
 * something unscannable). */
bool remote_qr_can_encode(const char* payload);

bool remote_qr_module_is_dark(const RemoteQrMatrix* matrix, int x, int y);

/* An XBM bitmap covering the whole code area, the symbol drawn at two pixels
 * per module and centred, so a smaller version keeps a quiet zone inside the
 * area. Row stride is REMOTE_QR_XBM_ROW_BYTES; bit order is XBM's, least
 * significant bit leftmost. */
#define REMOTE_QR_XBM_ROW_BYTES ((REMOTE_LAYOUT_QR_SIZE + 7) / 8)
#define REMOTE_QR_XBM_LENGTH    (REMOTE_QR_XBM_ROW_BYTES * REMOTE_LAYOUT_QR_SIZE)

typedef struct {
    uint8_t bits[REMOTE_QR_XBM_LENGTH];
} RemoteQrBitmap;

void remote_qr_render_bitmap(const RemoteQrMatrix* matrix, RemoteQrBitmap* bitmap);

/* For tests and diagnostics: whether the given pixel of the bitmap is dark. */
bool remote_qr_bitmap_pixel_is_dark(const RemoteQrBitmap* bitmap, int x, int y);

#ifdef __cplusplus
}
#endif
