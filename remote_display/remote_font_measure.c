#include "remote_font_metrics.h"

static int glyph_advance(const unsigned char* advance_table, char character) {
    unsigned char code = (unsigned char)character;
    if(code < REMOTE_FONT_FIRST_GLYPH || code > REMOTE_FONT_LAST_GLYPH) {
        /* Outside the table: the firmware draws nothing for it either, and
         * a byte the protocol will never carry must not widen a string. */
        return 0;
    }
    return advance_table[code - REMOTE_FONT_FIRST_GLYPH];
}

int remote_font_text_width(RemoteLayoutFont font, const char* text, size_t character_count) {
    const unsigned char* advance_table = remote_font_advance_table(font);
    int width = 0;
    for(size_t character_index = 0; character_index < character_count && text[character_index] != '\0'; character_index++) {
        width += glyph_advance(advance_table, text[character_index]);
    }
    return width;
}

size_t remote_font_fit_prefix_length(RemoteLayoutFont font, const char* text, int max_width_pixels) {
    const unsigned char* advance_table = remote_font_advance_table(font);
    int width = 0;
    size_t character_index = 0;
    while(text[character_index] != '\0') {
        int next_width = width + glyph_advance(advance_table, text[character_index]);
        if(next_width > max_width_pixels) {
            break;
        }
        width = next_width;
        character_index++;
    }
    return character_index;
}
