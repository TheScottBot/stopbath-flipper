/*
 * Glyph advances for the two firmware fonts the layout uses, so a string can
 * be measured on the development machine exactly as the device will draw it.
 * The table is generated from the pinned firmware's font data, see
 * scripts/generate_font_metrics.py. No SDK dependency.
 */
#pragma once

#include <stddef.h>

#include "remote_display_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Printable ASCII, space through tilde. Anything outside has zero advance
 * here and is drawn as nothing by the firmware's font renderer too. */
#define REMOTE_FONT_FIRST_GLYPH 0x20
#define REMOTE_FONT_LAST_GLYPH  0x7E
#define REMOTE_FONT_GLYPH_COUNT (REMOTE_FONT_LAST_GLYPH - REMOTE_FONT_FIRST_GLYPH + 1)

/* Generated. */
const unsigned char* remote_font_advance_table(RemoteLayoutFont font);

/* Width in pixels of the first character_count characters of text. */
int remote_font_text_width(RemoteLayoutFont font, const char* text, size_t character_count);

/* The largest prefix length of text, in characters, whose width does not
 * exceed max_width_pixels. */
size_t remote_font_fit_prefix_length(RemoteLayoutFont font, const char* text, int max_width_pixels);

#ifdef __cplusplus
}
#endif
