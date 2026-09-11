/*
 * NDEF message construction for the NFC surface, from the same value the QR
 * is rendered from (specification 2.7: one value per page, both encodings
 * derived from it, so they cannot disagree).
 *
 * No SDK dependency, no allocation. Everything here is built from sources
 * read at the pinned firmware commit and from Android's own parser, recorded
 * in docs/evaluation/ACTUAL_CONTRACT_EVALUATION.md:
 *
 * - the NDEF record header (flags byte: message begin, message end, short
 *   record, type name format; then type length, payload length, type), as
 *   the firmware's NDEF parser walks it;
 * - the URI record, well known type "U", payload beginning with a prefix
 *   code from the firmware parser's table (0x03 is "http://");
 * - the Wi-Fi record, media type "application/vnd.wfa.wsc", a credential
 *   attribute 0x100E holding SSID 0x1045, authentication type 0x1003 and
 *   network key 0x1027, which is the whole of what Android's
 *   NfcWifiProtectedSetup reads.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bounds from the sources above and from the credential grammar: an SSID is
 * at most 32 bytes (IEEE 802.11), Android refuses a network key over 64
 * bytes, and the record overhead is fixed. The message buffer is sized for
 * the largest record this device will ever build. */
#define REMOTE_NDEF_MAX_SSID_LENGTH        32
#define REMOTE_NDEF_MAX_NETWORK_KEY_LENGTH 64
#define REMOTE_NDEF_MESSAGE_CAPACITY       256

typedef struct {
    uint8_t bytes[REMOTE_NDEF_MESSAGE_CAPACITY];
    size_t length;
} RemoteNdefMessage;

/* Builds a single URI record from a web address. An address beginning with
 * http:// or https:// (any case, the scheme being case insensitive) is
 * abbreviated with the standard prefix code; anything else is carried whole
 * with prefix code zero. Returns false, leaving the message empty, for an
 * empty address or one that would not fit. */
bool remote_ndef_build_uri_message(const char* address, RemoteNdefMessage* message);

/* Builds a single Wi-Fi credential record from a Wi-Fi code payload in the
 * grammar StopBath emits, WIFI:T:WPA;S:name;P:passphrase;; with backslash
 * escaping of the reserved characters. Returns false, leaving the message
 * empty, when the payload is not in that grammar, names a security type
 * other than WPA, or carries a name or passphrase outside the bounds above.
 * The credential is what Android reads: SSID, authentication type WPA2
 * personal, network key. */
bool remote_ndef_build_wifi_message(const char* wifi_payload, RemoteNdefMessage* message);

#ifdef __cplusplus
}
#endif
