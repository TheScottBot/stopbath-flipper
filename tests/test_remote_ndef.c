/*
 * NDEF record construction, brought forward from FE6 for the FD15 to FD17
 * experiment. The expected byte sequences are written out in full from the
 * sources recorded in the evaluation log, so the test is a vector, not a
 * restatement of the builder.
 *
 * FE6 adds the cross check against an independent implementation (ndeflib) in
 * tests/test_remote_ndef_vectors.c; this file keeps the grammar and refusal
 * cases (escaping, field order, malformed payloads, the buffer bound) that a
 * reference encoder does not exercise.
 */
#include "test_support.h"

#include "../remote_display/remote_ndef.h"

static bool bytes_equal(RemoteTestReport* report, const RemoteNdefMessage* message, const uint8_t* expected, size_t expected_length, const char* description) {
    REMOTE_TEST_ASSERT_EQUAL_INT(report, expected_length, message->length, description);
    if(message->length != expected_length) return false;
    bool same = memcmp(message->bytes, expected, expected_length) == 0;
    REMOTE_TEST_ASSERT(report, same, description);
    if(!same) {
        for(size_t byte_index = 0; byte_index < expected_length; byte_index++) {
            if(message->bytes[byte_index] != expected[byte_index]) {
                fprintf(stderr, "    first difference at byte %zu: expected %02X got %02X\n", byte_index, expected[byte_index], message->bytes[byte_index]);
                break;
            }
        }
    }
    return same;
}

static void the_gallery_address_becomes_a_short_uri_record_with_the_http_prefix(RemoteTestReport* report) {
    /* Header 0xD1: message begin, message end, short record, well known
     * type. Type length 1, payload length 14, type "U", prefix 0x03 for
     * "http://", then the rest of the address. The upper case scheme in the
     * QR payload is folded away by the prefix, which is the same address. */
    static const uint8_t expected[] = {
        0xD1, 0x01, 0x0E, 'U', 0x03,
        '1', '9', '2', '.', '1', '6', '8', '.', '7', '2', '.', '1', '/',
    };
    RemoteNdefMessage message;
    REMOTE_TEST_ASSERT(report, remote_ndef_build_uri_message("HTTP://192.168.72.1/", &message), "builds");
    bytes_equal(report, &message, expected, sizeof(expected), "uri record bytes");
}

static void an_https_address_uses_prefix_four_and_an_unknown_scheme_prefix_zero(RemoteTestReport* report) {
    static const uint8_t expected_https[] = {0xD1, 0x01, 0x04, 'U', 0x04, 'a', '/', 'b'};
    RemoteNdefMessage message;
    REMOTE_TEST_ASSERT(report, remote_ndef_build_uri_message("https://a/b", &message), "builds https");
    bytes_equal(report, &message, expected_https, sizeof(expected_https), "https prefix");

    static const uint8_t expected_plain[] = {0xD1, 0x01, 0x04, 'U', 0x00, 'x', ':', 'y'};
    REMOTE_TEST_ASSERT(report, remote_ndef_build_uri_message("x:y", &message), "builds other");
    bytes_equal(report, &message, expected_plain, sizeof(expected_plain), "no prefix");
}

static void an_empty_or_oversized_address_is_refused(RemoteTestReport* report) {
    RemoteNdefMessage message;
    REMOTE_TEST_ASSERT(report, !remote_ndef_build_uri_message("", &message), "empty refused");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 0, message.length, "left empty");
    char oversized[REMOTE_NDEF_MESSAGE_CAPACITY + 8];
    memset(oversized, 'a', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = '\0';
    REMOTE_TEST_ASSERT(report, !remote_ndef_build_uri_message(oversized, &message), "oversized refused");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 0, message.length, "left empty");
}

static void the_wifi_payload_becomes_a_wsc_credential_record(RemoteTestReport* report) {
    /* Header 0xD2: message begin, message end, short record, media type.
     * Type length 23, payload length 41 (credential header 4, SSID attribute
     * 4 + 15, auth type attribute 4 + 2, key attribute 4 + 8). Attributes are
     * big endian id and length, as Android reads them. */
    static const uint8_t expected[] = {
        0xD2, 0x17, 0x29,
        'a', 'p', 'p', 'l', 'i', 'c', 'a', 't', 'i', 'o', 'n', '/', 'v', 'n', 'd', '.', 'w', 'f', 'a', '.', 'w', 's', 'c',
        0x10, 0x0E, 0x00, 0x25,
        0x10, 0x45, 0x00, 0x0F,
        'S', 't', 'o', 'p', 'B', 'a', 't', 'h', 'E', 'x', 'a', 'm', 'p', 'l', 'e',
        0x10, 0x03, 0x00, 0x02, 0x00, 0x20,
        0x10, 0x27, 0x00, 0x08,
        'e', 'x', 'a', 'm', 'p', 'l', 'e', '1',
    };
    RemoteNdefMessage message;
    REMOTE_TEST_ASSERT(report, remote_ndef_build_wifi_message("WIFI:T:WPA;S:StopBathExample;P:example1;;", &message), "builds");
    bytes_equal(report, &message, expected, sizeof(expected), "wifi record bytes");
}

static void escaped_reserved_characters_are_unescaped_into_the_record(RemoteTestReport* report) {
    /* StopBath's own test vector for the grammar: an SSID with a semicolon
     * and a passphrase with a colon, a comma, quotes and a backslash. */
    RemoteNdefMessage message;
    REMOTE_TEST_ASSERT(report, remote_ndef_build_wifi_message("WIFI:T:WPA;S:Stop\\;Bath;P:pass\\:word\\,with\\\"quotes\\\\;;", &message), "builds");
    /* SSID attribute value follows the 4 byte credential header and the 4
     * byte attribute header inside the payload, which starts after the 3
     * byte record header and the 23 byte type. */
    size_t ssid_offset = 3 + 23 + 4 + 4;
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 9, message.bytes[ssid_offset - 1], "ssid length is nine after unescaping");
    REMOTE_TEST_ASSERT(report, memcmp(&message.bytes[ssid_offset], "Stop;Bath", 9) == 0, "ssid unescaped");
    size_t key_offset = ssid_offset + 9 + 6 + 4;
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 22, message.bytes[key_offset - 1], "key length after unescaping");
    REMOTE_TEST_ASSERT(report, memcmp(&message.bytes[key_offset], "pass:word,with\"quotes\\", 22) == 0, "key unescaped");
}

static void field_order_in_the_payload_does_not_matter(RemoteTestReport* report) {
    RemoteNdefMessage in_order;
    RemoteNdefMessage reordered;
    REMOTE_TEST_ASSERT(report, remote_ndef_build_wifi_message("WIFI:T:WPA;S:abc;P:12345678;;", &in_order), "builds");
    REMOTE_TEST_ASSERT(report, remote_ndef_build_wifi_message("WIFI:S:abc;P:12345678;T:WPA;;", &reordered), "builds reordered");
    REMOTE_TEST_ASSERT_EQUAL_INT(report, in_order.length, reordered.length, "same length");
    REMOTE_TEST_ASSERT(report, memcmp(in_order.bytes, reordered.bytes, in_order.length) == 0, "same bytes");
}

typedef struct {
    const char* payload;
    const char* description;
} RefusedPayload;

static void malformed_or_unsupported_payloads_are_refused(RemoteTestReport* report) {
    char long_ssid_payload[128];
    snprintf(long_ssid_payload, sizeof(long_ssid_payload), "WIFI:T:WPA;S:%.33s;P:12345678;;", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    char long_key_payload[160];
    snprintf(long_key_payload, sizeof(long_key_payload), "WIFI:T:WPA;S:abc;P:%.65s;;", "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    const RefusedPayload refused[] = {
        {"", "empty"},
        {"HTTP://192.168.72.1/", "not a wifi payload"},
        {"WIFI:T:WEP;S:abc;P:12345678;;", "wep is not carried"},
        {"WIFI:T:nopass;S:abc;;", "open network is not carried"},
        {"WIFI:T:WPA;P:12345678;;", "no ssid"},
        {"WIFI:T:WPA;S:abc;;", "no passphrase"},
        {"WIFI:S:abc;P:12345678;;", "no security type"},
        {"WIFI:T:WPA;S:;P:12345678;;", "empty ssid"},
        {"WIFI:T:WPA;S:abc;P:;;", "empty passphrase"},
        {"WIFI:T:WPA;S:abc;P:12345678", "unterminated"},
        {"WIFI:T:WPA;S:abc;P:12345678;;trailing", "trailing text"},
        {"WIFI:T:WPA;S:abc;P:1234\\", "dangling escape"},
        {"WIFI:T:WPA;X:1;S:abc;P:12345678;;", "unknown field"},
        {long_ssid_payload, "ssid over 32"},
        {long_key_payload, "key over 64"},
    };
    for(int row_index = 0; row_index < REMOTE_TEST_ROW_COUNT(refused); row_index++) {
        RemoteNdefMessage message;
        memset(&message, 0x5A, sizeof(message));
        REMOTE_TEST_ASSERT(report, !remote_ndef_build_wifi_message(refused[row_index].payload, &message), refused[row_index].description);
        REMOTE_TEST_ASSERT_EQUAL_INT(report, 0, message.length, refused[row_index].description);
    }
}

static void the_largest_credential_fits_the_message_buffer(RemoteTestReport* report) {
    char payload[160];
    snprintf(payload, sizeof(payload), "WIFI:T:WPA;S:%.32s;P:%.64s;;", "ssssssssssssssssssssssssssssssssssss", "kkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkk");
    RemoteNdefMessage message;
    REMOTE_TEST_ASSERT(report, remote_ndef_build_wifi_message(payload, &message), "largest credential builds");
    REMOTE_TEST_ASSERT(report, message.length <= REMOTE_NDEF_MESSAGE_CAPACITY, "within capacity");
    /* 3 header + 23 type + 4 credential + (4 + 32) + (4 + 2) + (4 + 64). */
    REMOTE_TEST_ASSERT_EQUAL_INT(report, 140, message.length, "exact length");
}

int main(void) {
    static const RemoteTestCase test_cases[] = {
        {"the gallery address becomes a short uri record with the http prefix", the_gallery_address_becomes_a_short_uri_record_with_the_http_prefix},
        {"an https address uses prefix four and an unknown scheme prefix zero", an_https_address_uses_prefix_four_and_an_unknown_scheme_prefix_zero},
        {"an empty or oversized address is refused", an_empty_or_oversized_address_is_refused},
        {"the wifi payload becomes a wsc credential record", the_wifi_payload_becomes_a_wsc_credential_record},
        {"escaped reserved characters are unescaped into the record", escaped_reserved_characters_are_unescaped_into_the_record},
        {"field order in the payload does not matter", field_order_in_the_payload_does_not_matter},
        {"malformed or unsupported payloads are refused", malformed_or_unsupported_payloads_are_refused},
        {"the largest credential fits the message buffer", the_largest_credential_fits_the_message_buffer},
    };
    return remote_test_run_all(test_cases, REMOTE_TEST_ROW_COUNT(test_cases));
}
