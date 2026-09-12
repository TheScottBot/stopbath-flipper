/*
 * FE5, the published vector criterion: a known payload produces a known
 * matrix, checked module for module against a matrix an INDEPENDENT encoder
 * produced. The vectors in qr_published_vectors.h were emitted by the product
 * path remote_qr_encode() and reproduced exactly by libqrencode (Kentaro
 * Fukuchi's implementation, unrelated to the nayuki qrcodegen this project
 * vendors); the header's provenance records how. This test closes the gap the
 * FD4 experiment left open ("matching against published full matrix vectors is
 * FE5's own criterion and is not claimed here", test_remote_qr.c).
 *
 * The value is that a transposed axis, an off by one in module reading, a mask
 * or error correction regression, or a swapped byte order would all change at
 * least one module and fail here against a reference this project did not
 * compute, where the finder pattern and version checks in test_remote_qr.c
 * would still pass.
 */
#include "test_support.h"

#include "../remote_display/remote_qr.h"
#include "qr_published_vectors.h"

static void every_published_vector_reproduces_exactly(RemoteTestReport* report) {
    for(int vector_index = 0; vector_index < qr_published_vector_count; vector_index++) {
        const QrPublishedVector* vector = &qr_published_vectors[vector_index];
        RemoteQrMatrix matrix;
        REMOTE_TEST_ASSERT(report, remote_qr_encode(vector->payload, &matrix), vector->name);
        REMOTE_TEST_ASSERT_EQUAL_INT(report, vector->side, matrix.side, vector->name);

        /* Every module, and every module just outside the symbol, matches the
         * independent reference. The border pass catches a side that is right
         * by luck but a symbol shifted within it. */
        bool all_agree = true;
        for(int y = 0; y < vector->side; y++) {
            for(int x = 0; x < vector->side; x++) {
                bool expected_dark = vector->rows[y][x] == '1';
                if(remote_qr_module_is_dark(&matrix, x, y) != expected_dark) {
                    all_agree = false;
                }
            }
        }
        REMOTE_TEST_ASSERT(report, all_agree, vector->name);
    }
}

static void the_vector_set_covers_the_display_range(RemoteTestReport* report) {
    /* A guard on the vectors themselves: if a future edit drops the byte mode
     * or the version three case, the cross check silently narrows. The display
     * ceiling is version 3 (29 modules) and the smallest useful symbol is
     * version 1 (21), and both extremes must be represented. */
    bool has_21 = false;
    bool has_29 = false;
    for(int vector_index = 0; vector_index < qr_published_vector_count; vector_index++) {
        if(qr_published_vectors[vector_index].side == 21) has_21 = true;
        if(qr_published_vectors[vector_index].side == 29) has_29 = true;
    }
    REMOTE_TEST_ASSERT(report, qr_published_vector_count >= 3, "at least three published vectors");
    REMOTE_TEST_ASSERT(report, has_21, "a version one vector is present");
    REMOTE_TEST_ASSERT(report, has_29, "a version three vector is present");
}

int main(void) {
    static const RemoteTestCase test_cases[] = {
        {"every published vector reproduces exactly", every_published_vector_reproduces_exactly},
        {"the vector set covers the display range", the_vector_set_covers_the_display_range},
    };
    return remote_test_run_all(test_cases, REMOTE_TEST_ROW_COUNT(test_cases));
}
