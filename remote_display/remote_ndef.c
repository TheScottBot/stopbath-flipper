#include "remote_ndef.h"

#include <string.h>

/* NDEF record header flag bits, as the firmware's parser reads them. */
#define NDEF_FLAG_MESSAGE_BEGIN 0x80
#define NDEF_FLAG_MESSAGE_END   0x40
#define NDEF_FLAG_SHORT_RECORD  0x10
#define NDEF_TNF_WELL_KNOWN     0x01
#define NDEF_TNF_MEDIA_TYPE     0x02

/* URI record prefix codes from the firmware parser's table. */
#define NDEF_URI_PREFIX_NONE  0x00
#define NDEF_URI_PREFIX_HTTP  0x03
#define NDEF_URI_PREFIX_HTTPS 0x04

/* The Wi-Fi Simple Configuration attributes Android reads. */
#define WSC_MIME_TYPE               "application/vnd.wfa.wsc"
#define WSC_ATTRIBUTE_CREDENTIAL    0x100E
#define WSC_ATTRIBUTE_SSID          0x1045
#define WSC_ATTRIBUTE_AUTH_TYPE     0x1003
#define WSC_ATTRIBUTE_NETWORK_KEY   0x1027
#define WSC_AUTH_TYPE_WPA2_PERSONAL 0x0020
#define WSC_ATTRIBUTE_HEADER_LENGTH 4

/* A short record carries a one byte payload length. */
#define NDEF_SHORT_RECORD_MAX_PAYLOAD 255

typedef struct {
    RemoteNdefMessage* message;
    bool overflowed;
} MessageWriter;

static void write_byte(MessageWriter* writer, uint8_t value) {
    if(writer->message->length >= REMOTE_NDEF_MESSAGE_CAPACITY) {
        writer->overflowed = true;
        return;
    }
    writer->message->bytes[writer->message->length++] = value;
}

static void write_bytes(MessageWriter* writer, const void* source, size_t length) {
    const uint8_t* source_bytes = source;
    for(size_t byte_index = 0; byte_index < length; byte_index++) {
        write_byte(writer, source_bytes[byte_index]);
    }
}

static void write_big_endian_16(MessageWriter* writer, unsigned int value) {
    write_byte(writer, (uint8_t)(value >> 8));
    write_byte(writer, (uint8_t)(value & 0xFF));
}

/* One short record making up the whole message: begin and end both set. */
static bool write_single_short_record(
    RemoteNdefMessage* message,
    uint8_t type_name_format,
    const char* type,
    const uint8_t* payload,
    size_t payload_length) {
    memset(message, 0, sizeof(*message));
    size_t type_length = strlen(type);
    if(payload_length > NDEF_SHORT_RECORD_MAX_PAYLOAD || type_length > 255) {
        return false;
    }
    MessageWriter writer = {message, false};
    write_byte(&writer, (uint8_t)(NDEF_FLAG_MESSAGE_BEGIN | NDEF_FLAG_MESSAGE_END | NDEF_FLAG_SHORT_RECORD | type_name_format));
    write_byte(&writer, (uint8_t)type_length);
    write_byte(&writer, (uint8_t)payload_length);
    write_bytes(&writer, type, type_length);
    write_bytes(&writer, payload, payload_length);
    if(writer.overflowed) {
        memset(message, 0, sizeof(*message));
        return false;
    }
    return true;
}

static bool starts_with_case_insensitive(const char* text, const char* prefix) {
    for(size_t index = 0; prefix[index] != '\0'; index++) {
        char text_character = text[index];
        char prefix_character = prefix[index];
        if(text_character >= 'A' && text_character <= 'Z') text_character = (char)(text_character - 'A' + 'a');
        if(text_character != prefix_character) return false;
    }
    return true;
}

bool remote_ndef_build_uri_message(const char* address, RemoteNdefMessage* message) {
    memset(message, 0, sizeof(*message));
    if(address == NULL || address[0] == '\0') {
        return false;
    }

    uint8_t prefix_code = NDEF_URI_PREFIX_NONE;
    const char* remainder = address;
    if(starts_with_case_insensitive(address, "https://")) {
        prefix_code = NDEF_URI_PREFIX_HTTPS;
        remainder = address + strlen("https://");
    } else if(starts_with_case_insensitive(address, "http://")) {
        prefix_code = NDEF_URI_PREFIX_HTTP;
        remainder = address + strlen("http://");
    }

    size_t remainder_length = strlen(remainder);
    if(remainder_length + 1 > NDEF_SHORT_RECORD_MAX_PAYLOAD) {
        return false;
    }
    uint8_t payload[NDEF_SHORT_RECORD_MAX_PAYLOAD];
    payload[0] = prefix_code;
    memcpy(&payload[1], remainder, remainder_length);
    return write_single_short_record(message, NDEF_TNF_WELL_KNOWN, "U", payload, remainder_length + 1);
}

/* The Wi-Fi code grammar: WIFI: then key:value fields each ended by an
 * unescaped semicolon, the message ended by a second semicolon. A backslash
 * escapes the next character. Values are copied unescaped into fixed
 * buffers; a value that does not fit refuses the whole payload. */
typedef struct {
    char security_type[8];
    char ssid[REMOTE_NDEF_MAX_SSID_LENGTH + 1];
    char passphrase[REMOTE_NDEF_MAX_NETWORK_KEY_LENGTH + 1];
    bool has_security_type;
    bool has_ssid;
    bool has_passphrase;
} WifiCodeFields;

/* Reads one value up to an unescaped semicolon. Returns the position after
 * that semicolon, or NULL on a dangling escape, an unterminated value, or a
 * value longer than the buffer. */
static const char* read_escaped_value(const char* cursor, char* value, size_t value_capacity) {
    size_t value_length = 0;
    while(*cursor != ';') {
        if(*cursor == '\0') return NULL;
        if(*cursor == '\\') {
            cursor++;
            if(*cursor == '\0') return NULL;
        }
        if(value_length + 1 >= value_capacity) return NULL;
        value[value_length++] = *cursor;
        cursor++;
    }
    value[value_length] = '\0';
    return cursor + 1;
}

static bool parse_wifi_code(const char* wifi_payload, WifiCodeFields* fields) {
    memset(fields, 0, sizeof(*fields));
    if(strncmp(wifi_payload, "WIFI:", 5) != 0) return false;
    const char* cursor = wifi_payload + 5;

    while(*cursor != ';') {
        if(cursor[0] == '\0' || cursor[1] != ':') return false;
        char field_key = cursor[0];
        cursor += 2;
        switch(field_key) {
        case 'T':
            cursor = read_escaped_value(cursor, fields->security_type, sizeof(fields->security_type));
            fields->has_security_type = true;
            break;
        case 'S':
            cursor = read_escaped_value(cursor, fields->ssid, sizeof(fields->ssid));
            fields->has_ssid = true;
            break;
        case 'P':
            cursor = read_escaped_value(cursor, fields->passphrase, sizeof(fields->passphrase));
            fields->has_passphrase = true;
            break;
        default:
            /* The grammar has other keys (hidden, EAP parameters) that the
             * appliance never emits; refusing is safer than guessing. */
            return false;
        }
        if(cursor == NULL) return false;
    }
    /* The closing semicolon must end the payload: anything after it is not
     * part of the grammar and would be silently dropped otherwise. */
    return cursor[1] == '\0';
}

static void write_wsc_attribute(MessageWriter* writer, unsigned int attribute_id, const void* value, size_t value_length) {
    write_big_endian_16(writer, attribute_id);
    write_big_endian_16(writer, (unsigned int)value_length);
    write_bytes(writer, value, value_length);
}

bool remote_ndef_build_wifi_message(const char* wifi_payload, RemoteNdefMessage* message) {
    memset(message, 0, sizeof(*message));
    if(wifi_payload == NULL) return false;

    WifiCodeFields fields;
    if(!parse_wifi_code(wifi_payload, &fields)) return false;
    if(!fields.has_security_type || !fields.has_ssid || !fields.has_passphrase) return false;
    /* WPA in this grammar is what StopBath pins (WPA2 personal). Other
     * types would need other attributes and the appliance never sends them. */
    if(strcmp(fields.security_type, "WPA") != 0) return false;
    size_t ssid_length = strlen(fields.ssid);
    size_t passphrase_length = strlen(fields.passphrase);
    if(ssid_length == 0 || passphrase_length == 0) return false;

    /* The credential attribute wraps the three attributes Android reads. */
    uint8_t auth_type[2] = {(uint8_t)(WSC_AUTH_TYPE_WPA2_PERSONAL >> 8), (uint8_t)(WSC_AUTH_TYPE_WPA2_PERSONAL & 0xFF)};
    size_t credential_length = (WSC_ATTRIBUTE_HEADER_LENGTH + ssid_length) +
                               (WSC_ATTRIBUTE_HEADER_LENGTH + sizeof(auth_type)) +
                               (WSC_ATTRIBUTE_HEADER_LENGTH + passphrase_length);

    RemoteNdefMessage payload_message;
    memset(&payload_message, 0, sizeof(payload_message));
    MessageWriter payload_writer = {&payload_message, false};
    write_big_endian_16(&payload_writer, WSC_ATTRIBUTE_CREDENTIAL);
    write_big_endian_16(&payload_writer, (unsigned int)credential_length);
    write_wsc_attribute(&payload_writer, WSC_ATTRIBUTE_SSID, fields.ssid, ssid_length);
    write_wsc_attribute(&payload_writer, WSC_ATTRIBUTE_AUTH_TYPE, auth_type, sizeof(auth_type));
    write_wsc_attribute(&payload_writer, WSC_ATTRIBUTE_NETWORK_KEY, fields.passphrase, passphrase_length);
    if(payload_writer.overflowed) return false;

    return write_single_short_record(message, NDEF_TNF_MEDIA_TYPE, WSC_MIME_TYPE, payload_message.bytes, payload_message.length);
}
