#include "remote_qr.h"

#include <string.h>

#include <qrcodegen.h>

bool remote_qr_encode(const char* payload, RemoteQrMatrix* matrix) {
    memset(matrix, 0, sizeof(*matrix));
    if(payload == NULL || payload[0] == '\0') {
        return false;
    }

    /* The library writes before it reads, so neither buffer needs
     * initialising. Both are sized for the ceiling version and live on the
     * stack for the duration of the call: 106 bytes each, never on the heap. */
    uint8_t scratch[REMOTE_QR_BUFFER_LENGTH];
    bool encoded = qrcodegen_encodeText(
        payload,
        scratch,
        matrix->encoded,
        /* Lowest level first so the display ceiling is not reached early;
         * boostEcl lets the library raise it when the same version has room,
         * which is free error tolerance. */
        qrcodegen_Ecc_LOW,
        qrcodegen_VERSION_MIN,
        REMOTE_QR_MAX_VERSION,
        qrcodegen_Mask_AUTO,
        true);
    if(!encoded) {
        memset(matrix, 0, sizeof(*matrix));
        return false;
    }
    matrix->side = qrcodegen_getSize(matrix->encoded);
    return true;
}

bool remote_qr_module_is_dark(const RemoteQrMatrix* matrix, int x, int y) {
    if(matrix->side == 0 || x < 0 || y < 0 || x >= matrix->side || y >= matrix->side) {
        return false;
    }
    return qrcodegen_getModule(matrix->encoded, x, y);
}

void remote_qr_render_bitmap(const RemoteQrMatrix* matrix, RemoteQrBitmap* bitmap) {
    memset(bitmap, 0, sizeof(*bitmap));
    if(matrix->side == 0) {
        return;
    }
    /* Centred, so a version below the ceiling keeps a quiet zone inside the
     * code area rather than hugging one edge of it. */
    int symbol_pixels = matrix->side * REMOTE_QR_PIXELS_PER_MODULE;
    int origin = (REMOTE_LAYOUT_QR_SIZE - symbol_pixels) / 2;

    for(int module_y = 0; module_y < matrix->side; module_y++) {
        for(int module_x = 0; module_x < matrix->side; module_x++) {
            if(!qrcodegen_getModule(matrix->encoded, module_x, module_y)) {
                continue;
            }
            for(int pixel_y = 0; pixel_y < REMOTE_QR_PIXELS_PER_MODULE; pixel_y++) {
                for(int pixel_x = 0; pixel_x < REMOTE_QR_PIXELS_PER_MODULE; pixel_x++) {
                    int x = origin + module_x * REMOTE_QR_PIXELS_PER_MODULE + pixel_x;
                    int y = origin + module_y * REMOTE_QR_PIXELS_PER_MODULE + pixel_y;
                    /* XBM: one bit per pixel, least significant bit leftmost. */
                    bitmap->bits[y * REMOTE_QR_XBM_ROW_BYTES + x / 8] |= (uint8_t)(1u << (x % 8));
                }
            }
        }
    }
}

bool remote_qr_bitmap_pixel_is_dark(const RemoteQrBitmap* bitmap, int x, int y) {
    if(x < 0 || y < 0 || x >= REMOTE_LAYOUT_QR_SIZE || y >= REMOTE_LAYOUT_QR_SIZE) {
        return false;
    }
    return ((bitmap->bits[y * REMOTE_QR_XBM_ROW_BYTES + x / 8] >> (x % 8)) & 1) != 0;
}
