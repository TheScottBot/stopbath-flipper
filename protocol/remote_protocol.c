#include "remote_protocol.h"

#include <string.h>

/* Printable ASCII: what the wire and every decoded value are confined to. */
#define LOWEST_PRINTABLE  0x20
#define HIGHEST_PRINTABLE 0x7E

/* An integer field has at most as many digits as the largest bound in the
 * table, five, so anything longer is refused before it can overflow. */
#define MAX_INTEGER_DIGITS 10

static bool is_printable(uint8_t byte) {
    return byte >= LOWEST_PRINTABLE && byte <= HIGHEST_PRINTABLE;
}

static bool is_unreserved(uint8_t byte) {
    return strchr(REMOTE_PROTOCOL_UNRESERVED, (int)byte) != NULL && byte != '\0';
}

static bool is_token_character(uint8_t byte) {
    return (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') || byte == '-';
}

static const RemoteProtocolFieldDescriptor* field_descriptor(RemoteProtocolVerb verb, int field_index) {
    const RemoteProtocolVerbDescriptor* verb_descriptor = remote_protocol_verb_descriptor(verb);
    if(verb_descriptor == NULL || field_index < 0 || field_index >= verb_descriptor->field_count) {
        return NULL;
    }
    return &verb_descriptor->fields[field_index];
}

/* Whether a text value is acceptable for its field: length within the
 * field's bound, and every byte printable (text) or a token character
 * (token). Shared by the setter, the encoder and the parser so no path can
 * accept what another refuses. */
static bool text_is_valid(const RemoteProtocolFieldDescriptor* field, const char* text, size_t text_length) {
    if(text_length > field->maximum_length) return false;
    if(field->type == RemoteProtocolFieldTypeToken && text_length == 0) return false;
    for(size_t index = 0; index < text_length; index++) {
        uint8_t byte = (uint8_t)text[index];
        if(field->type == RemoteProtocolFieldTypeToken) {
            if(!is_token_character(byte)) return false;
        } else {
            if(!is_printable(byte)) return false;
        }
    }
    return true;
}

static bool integer_is_valid(const RemoteProtocolFieldDescriptor* field, uint32_t value) {
    switch(field->type) {
    case RemoteProtocolFieldTypeInteger:
        return value >= field->minimum && value <= field->maximum;
    case RemoteProtocolFieldTypeBoolean:
        return value <= 1;
    case RemoteProtocolFieldTypeEnumeration: {
        int value_count = 0;
        remote_protocol_enumeration_values(field->enumeration, &value_count);
        return value < (uint32_t)value_count;
    }
    case RemoteProtocolFieldTypeToken:
    case RemoteProtocolFieldTypeText:
        break;
    }
    return false;
}

void remote_protocol_message_initialise(RemoteProtocolMessage* message, RemoteProtocolVerb verb) {
    memset(message, 0, sizeof(*message));
    message->verb = verb;
}

bool remote_protocol_message_set_text(RemoteProtocolMessage* message, int field_index, const char* text) {
    const RemoteProtocolFieldDescriptor* field = field_descriptor(message->verb, field_index);
    if(field == NULL || text == NULL) return false;
    if(field->type != RemoteProtocolFieldTypeToken && field->type != RemoteProtocolFieldTypeText) return false;
    size_t text_length = strlen(text);
    RemoteProtocolFieldValue* value = &message->fields[field_index];
    if(!text_is_valid(field, text, text_length)) {
        value->text[0] = '\0';
        value->text_length = 0;
        return false;
    }
    memcpy(value->text, text, text_length);
    value->text[text_length] = '\0';
    value->text_length = text_length;
    return true;
}

void remote_protocol_message_set_integer(RemoteProtocolMessage* message, int field_index, uint32_t value) {
    if(field_index < 0 || field_index >= REMOTE_PROTOCOL_MAX_FIELDS_PER_VERB) return;
    message->fields[field_index].integer = value;
}

/* The encoder. */

typedef struct {
    char* line;
    size_t capacity;
    size_t length;
    bool overflowed;
} LineWriter;

static void write_byte(LineWriter* writer, char byte) {
    if(writer->length >= writer->capacity) {
        writer->overflowed = true;
        return;
    }
    writer->line[writer->length++] = byte;
}

static void write_string(LineWriter* writer, const char* text) {
    for(size_t index = 0; text[index] != '\0'; index++) {
        write_byte(writer, text[index]);
    }
}

static void write_unsigned(LineWriter* writer, uint32_t value) {
    char digits[MAX_INTEGER_DIGITS + 1];
    int digit_count = 0;
    do {
        digits[digit_count++] = (char)('0' + (value % 10));
        value /= 10;
    } while(value > 0 && digit_count < MAX_INTEGER_DIGITS);
    while(digit_count > 0) {
        write_byte(writer, digits[--digit_count]);
    }
}

static void write_percent_encoded(LineWriter* writer, const char* text, size_t text_length) {
    static const char hexadecimal[] = "0123456789ABCDEF";
    for(size_t index = 0; index < text_length; index++) {
        uint8_t byte = (uint8_t)text[index];
        if(is_unreserved(byte)) {
            write_byte(writer, (char)byte);
        } else {
            write_byte(writer, REMOTE_PROTOCOL_ESCAPE);
            write_byte(writer, hexadecimal[byte >> 4]);
            write_byte(writer, hexadecimal[byte & 0x0F]);
        }
    }
}

bool remote_protocol_encode(const RemoteProtocolMessage* message, char* line, size_t line_capacity, size_t* line_length) {
    *line_length = 0;
    const RemoteProtocolVerbDescriptor* verb = remote_protocol_verb_descriptor(message->verb);
    if(verb == NULL) return false;

    /* Validate everything before writing anything, so a refused message
     * leaves the caller's buffer untouched. */
    for(int field_index = 0; field_index < verb->field_count; field_index++) {
        const RemoteProtocolFieldDescriptor* field = &verb->fields[field_index];
        const RemoteProtocolFieldValue* value = &message->fields[field_index];
        if(field->type == RemoteProtocolFieldTypeToken || field->type == RemoteProtocolFieldTypeText) {
            if(value->text_length > REMOTE_PROTOCOL_MAX_TEXT_LENGTH) return false;
            if(!text_is_valid(field, value->text, value->text_length)) return false;
        } else {
            if(!integer_is_valid(field, value->integer)) return false;
        }
    }

    /* The line may not exceed the message bound whatever the caller's
     * buffer holds, so the smaller of the two is the capacity. */
    size_t capacity = line_capacity < REMOTE_PROTOCOL_MAXIMUM_MESSAGE_LENGTH ? line_capacity : REMOTE_PROTOCOL_MAXIMUM_MESSAGE_LENGTH;
    LineWriter writer = {line, capacity, 0, false};
    write_string(&writer, verb->name);
    for(int field_index = 0; field_index < verb->field_count; field_index++) {
        const RemoteProtocolFieldDescriptor* field = &verb->fields[field_index];
        const RemoteProtocolFieldValue* value = &message->fields[field_index];
        write_byte(&writer, REMOTE_PROTOCOL_FIELD_SEPARATOR);
        write_string(&writer, field->name);
        write_byte(&writer, REMOTE_PROTOCOL_NAME_VALUE_SEPARATOR);
        switch(field->type) {
        case RemoteProtocolFieldTypeInteger:
        case RemoteProtocolFieldTypeBoolean:
            write_unsigned(&writer, value->integer);
            break;
        case RemoteProtocolFieldTypeEnumeration: {
            int value_count = 0;
            const char* const* values = remote_protocol_enumeration_values(field->enumeration, &value_count);
            write_string(&writer, values[value->integer]);
            break;
        }
        case RemoteProtocolFieldTypeToken:
            write_string(&writer, value->text);
            break;
        case RemoteProtocolFieldTypeText:
            write_percent_encoded(&writer, value->text, value->text_length);
            break;
        }
    }
    write_byte(&writer, REMOTE_PROTOCOL_TERMINATOR);
    if(writer.overflowed) {
        return false;
    }
    *line_length = writer.length;
    return true;
}

/* The parser. */

static RemoteProtocolFeedOutcome outcome_error(RemoteProtocolError error) {
    RemoteProtocolFeedOutcome outcome = {RemoteProtocolFeedOutcomeError, error};
    return outcome;
}

static RemoteProtocolFeedOutcome outcome_of_kind(RemoteProtocolFeedOutcomeKind kind) {
    RemoteProtocolFeedOutcome outcome = {kind, RemoteProtocolErrorNone};
    return outcome;
}

static int hexadecimal_digit_value(char character) {
    if(character >= '0' && character <= '9') return character - '0';
    /* Upper case only: the table says escapes are written upper case, and
     * accepting the other case would be a silent normalisation (0.11). */
    if(character >= 'A' && character <= 'F') return character - 'A' + 10;
    return -1;
}

/* Decodes a percent encoded value into the field. Returns the error to
 * report, or NONE. A decoded byte outside printable ASCII is malformed, not
 * merely invalid, because nothing legitimate produces it. */
static RemoteProtocolError decode_text(const RemoteProtocolFieldDescriptor* field, const char* encoded, size_t encoded_length, RemoteProtocolFieldValue* value) {
    size_t decoded_length = 0;
    size_t index = 0;
    while(index < encoded_length) {
        uint8_t byte = (uint8_t)encoded[index];
        uint8_t decoded;
        if(byte == REMOTE_PROTOCOL_ESCAPE) {
            /* Two characters must follow the escape. */
            if(index + 2 >= encoded_length) return RemoteProtocolErrorMalformed;
            int high = hexadecimal_digit_value(encoded[index + 1]);
            int low = hexadecimal_digit_value(encoded[index + 2]);
            if(high < 0 || low < 0) return RemoteProtocolErrorMalformed;
            decoded = (uint8_t)((high << 4) | low);
            index += 3;
        } else {
            /* Anything not unreserved had to be escaped. */
            if(!is_unreserved(byte)) return RemoteProtocolErrorMalformed;
            decoded = byte;
            index += 1;
        }
        if(!is_printable(decoded)) return RemoteProtocolErrorMalformed;
        if(decoded_length >= REMOTE_PROTOCOL_MAX_TEXT_LENGTH) return RemoteProtocolErrorBadValue;
        value->text[decoded_length++] = (char)decoded;
    }
    value->text[decoded_length] = '\0';
    value->text_length = decoded_length;
    if(!text_is_valid(field, value->text, decoded_length)) return RemoteProtocolErrorBadValue;
    return RemoteProtocolErrorNone;
}

static RemoteProtocolError decode_token(const RemoteProtocolFieldDescriptor* field, const char* raw, size_t raw_length, RemoteProtocolFieldValue* value) {
    if(raw_length > REMOTE_PROTOCOL_MAX_TEXT_LENGTH) return RemoteProtocolErrorBadValue;
    memcpy(value->text, raw, raw_length);
    value->text[raw_length] = '\0';
    value->text_length = raw_length;
    if(!text_is_valid(field, value->text, raw_length)) return RemoteProtocolErrorBadValue;
    return RemoteProtocolErrorNone;
}

static RemoteProtocolError decode_unsigned(const char* raw, size_t raw_length, uint32_t* out) {
    if(raw_length == 0 || raw_length > MAX_INTEGER_DIGITS) return RemoteProtocolErrorBadValue;
    /* No leading zero except the number zero itself: one spelling per value. */
    if(raw_length > 1 && raw[0] == '0') return RemoteProtocolErrorBadValue;
    uint32_t value = 0;
    for(size_t index = 0; index < raw_length; index++) {
        char character = raw[index];
        if(character < '0' || character > '9') return RemoteProtocolErrorBadValue;
        uint32_t digit = (uint32_t)(character - '0');
        if(value > (UINT32_MAX - digit) / 10) return RemoteProtocolErrorBadValue;
        value = value * 10 + digit;
    }
    *out = value;
    return RemoteProtocolErrorNone;
}

static RemoteProtocolError decode_enumeration(const RemoteProtocolFieldDescriptor* field, const char* raw, size_t raw_length, uint32_t* out) {
    int value_count = 0;
    const char* const* values = remote_protocol_enumeration_values(field->enumeration, &value_count);
    for(int value_index = 0; value_index < value_count; value_index++) {
        if(strlen(values[value_index]) == raw_length && memcmp(values[value_index], raw, raw_length) == 0) {
            *out = (uint32_t)value_index;
            return RemoteProtocolErrorNone;
        }
    }
    return RemoteProtocolErrorBadValue;
}

static RemoteProtocolError decode_field_value(const RemoteProtocolFieldDescriptor* field, const char* raw, size_t raw_length, RemoteProtocolFieldValue* value) {
    switch(field->type) {
    case RemoteProtocolFieldTypeInteger:
    case RemoteProtocolFieldTypeBoolean: {
        RemoteProtocolError error = decode_unsigned(raw, raw_length, &value->integer);
        if(error != RemoteProtocolErrorNone) return error;
        return integer_is_valid(field, value->integer) ? RemoteProtocolErrorNone : RemoteProtocolErrorBadValue;
    }
    case RemoteProtocolFieldTypeEnumeration:
        return decode_enumeration(field, raw, raw_length, &value->integer);
    case RemoteProtocolFieldTypeToken:
        return decode_token(field, raw, raw_length, value);
    case RemoteProtocolFieldTypeText:
        return decode_text(field, raw, raw_length, value);
    }
    return RemoteProtocolErrorBadValue;
}

/* Finds the end of the token starting at position: the next separator or
 * the end of the line. Tokens are never empty and separators are single. */
static size_t token_end(const char* line, size_t line_length, size_t position) {
    size_t end = position;
    while(end < line_length && line[end] != REMOTE_PROTOCOL_FIELD_SEPARATOR) {
        end++;
    }
    return end;
}

RemoteProtocolFeedOutcome remote_protocol_parse_line(const char* line, size_t line_length, RemoteProtocolMessage* message) {
    /* Structure first, in one pass over the bytes: printable throughout, no
     * empty token (which is what a leading, trailing or doubled separator
     * produces), and a line that is not empty. */
    if(line_length == 0) return outcome_error(RemoteProtocolErrorMalformed);
    for(size_t index = 0; index < line_length; index++) {
        if(!is_printable((uint8_t)line[index])) return outcome_error(RemoteProtocolErrorMalformed);
    }
    if(line[0] == REMOTE_PROTOCOL_FIELD_SEPARATOR || line[line_length - 1] == REMOTE_PROTOCOL_FIELD_SEPARATOR) {
        return outcome_error(RemoteProtocolErrorMalformed);
    }
    for(size_t index = 1; index < line_length; index++) {
        if(line[index] == REMOTE_PROTOCOL_FIELD_SEPARATOR && line[index - 1] == REMOTE_PROTOCOL_FIELD_SEPARATOR) {
            return outcome_error(RemoteProtocolErrorMalformed);
        }
    }

    /* The verb. */
    size_t verb_end = token_end(line, line_length, 0);
    const RemoteProtocolVerbDescriptor* verb = NULL;
    RemoteProtocolVerb verb_index = RemoteProtocolVerbCount;
    for(int candidate = 0; candidate < RemoteProtocolVerbCount; candidate++) {
        const RemoteProtocolVerbDescriptor* candidate_descriptor = remote_protocol_verb_descriptor((RemoteProtocolVerb)candidate);
        if(strlen(candidate_descriptor->name) == verb_end && memcmp(candidate_descriptor->name, line, verb_end) == 0) {
            verb = candidate_descriptor;
            verb_index = (RemoteProtocolVerb)candidate;
            break;
        }
    }
    if(verb == NULL) return outcome_error(RemoteProtocolErrorBadVerb);

    remote_protocol_message_initialise(message, verb_index);
    bool field_seen[REMOTE_PROTOCOL_MAX_FIELDS_PER_VERB] = {false};

    /* The fields, each name=value. Structural faults in a field (no
     * separator, no name) are malformed; an unknown or repeated name is a
     * field error; a bad value is a value error. */
    size_t position = verb_end;
    while(position < line_length) {
        position++; /* the separator */
        size_t end = token_end(line, line_length, position);
        const char* token = &line[position];
        size_t token_length = end - position;
        size_t equals = 0;
        while(equals < token_length && token[equals] != REMOTE_PROTOCOL_NAME_VALUE_SEPARATOR) {
            equals++;
        }
        if(equals == token_length || equals == 0) return outcome_error(RemoteProtocolErrorMalformed);

        int field_index = -1;
        for(int candidate = 0; candidate < verb->field_count; candidate++) {
            const char* name = verb->fields[candidate].name;
            if(strlen(name) == equals && memcmp(name, token, equals) == 0) {
                field_index = candidate;
                break;
            }
        }
        if(field_index < 0) return outcome_error(RemoteProtocolErrorBadField);
        if(field_seen[field_index]) return outcome_error(RemoteProtocolErrorBadField);
        field_seen[field_index] = true;

        RemoteProtocolError error = decode_field_value(&verb->fields[field_index], token + equals + 1, token_length - equals - 1, &message->fields[field_index]);
        if(error != RemoteProtocolErrorNone) return outcome_error(error);
        position = end;
    }

    for(int field_index = 0; field_index < verb->field_count; field_index++) {
        if(!field_seen[field_index]) return outcome_error(RemoteProtocolErrorNoField);
    }
    return outcome_of_kind(RemoteProtocolFeedOutcomeMessage);
}

void remote_protocol_line_assembler_initialise(RemoteProtocolLineAssembler* assembler) {
    memset(assembler, 0, sizeof(*assembler));
}

RemoteProtocolFeedOutcome remote_protocol_feed_byte(RemoteProtocolLineAssembler* assembler, uint8_t byte, RemoteProtocolMessage* message) {
    if(byte != (uint8_t)REMOTE_PROTOCOL_TERMINATOR) {
        /* The terminator counts towards the bound, so the line proper may
         * hold one byte fewer than the bound. Once over, nothing more is
         * kept; the line is refused when its end finally arrives. */
        if(assembler->line_length + 1 >= REMOTE_PROTOCOL_MAXIMUM_MESSAGE_LENGTH) {
            assembler->overlong = true;
        } else {
            if(!is_printable(byte)) assembler->unprintable = true;
            assembler->line[assembler->line_length++] = (char)byte;
        }
        return outcome_of_kind(RemoteProtocolFeedOutcomeIncomplete);
    }

    RemoteProtocolFeedOutcome outcome;
    if(assembler->overlong) {
        outcome = outcome_error(RemoteProtocolErrorTooLong);
    } else if(assembler->unprintable) {
        outcome = outcome_error(RemoteProtocolErrorMalformed);
    } else {
        outcome = remote_protocol_parse_line(assembler->line, assembler->line_length, message);
    }
    remote_protocol_line_assembler_initialise(assembler);
    return outcome;
}

bool remote_protocol_version_is_supported(uint32_t version) {
    /* Exact match against the supported set, which is the table's version
     * alone until a second version exists (PROTOCOL.md, version policy). */
    return version == REMOTE_PROTOCOL_VERSION;
}
