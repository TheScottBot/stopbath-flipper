# Protocol

## Where the definition lives, and when

The peripheral protocol is owned by StopBath and, once frozen, lives in the
StopBath repository (specification 2.8, extension 4.1). Until the freeze it is
drafted here, because the peripheral is built first (specification 2.9,
extension 1.4, `PD1`).

The sequence:

1. `FE1` and `FE2` had no protocol.
2. `FE3` drafts the protocol in this file and in `protocol.json`, builds the
   parser and encoder as a freestanding C library derived from that file, and
   builds the development peer. The appliance's stated requirements on the
   protocol are Part 4 of `docs/PERIPHERAL_SEAM.md` in the StopBath
   repository, and this draft is made against them.
3. From `FE3` until `FD20` the protocol is provisional and freely revisable.
   Every change is recorded below with its reason.
4. At `FD20` the author accepts it, it is promoted to the StopBath repository
   under review, field by field, and from then on this file references the
   promoted definition rather than carrying it. Rule 0.11 applies in full from
   that point.

## Status

PROVISIONAL. Drafted 2026-09-11. Nothing here is a sacred contract yet.

## The definition

`protocol.json` in this repository is the single normative table: the
version, the framing, every bound, every enumeration, and every verb with its
fields. The C parser and encoder tables are generated from it by
`scripts/generate_protocol_tables.py` into
`protocol/remote_protocol_tables.c`, and a test fails if the generated file
and the table ever diverge. This prose explains the table; where the two
disagree the table is wrong or the prose is, and the disagreement is a defect.

### Framing

One message per line, ended by a line feed. A line is the verb name, then the
verb's fields in any order, each written as `name=value`, separated by single
spaces. Every field of a verb is required and appears exactly once.

Values are percent encoded: a byte outside the unreserved set (ASCII letters,
digits, `-`, `.`, `_`, `~`) is written as `%` followed by two upper case
hexadecimal digits. This is how a payload carries a space, a semicolon, or a
colon, and how an empty payload is written (`payload=`). Nothing on the wire
is ever outside printable ASCII except the terminating line feed, and a
decoded payload is likewise confined to printable ASCII, which is what the
display can show.

A message longer than 512 bytes including its terminator is refused, and the
receiver discards bytes until the next line feed. A decoded payload longer
than 256 bytes is refused. Unknown verbs, unknown fields, missing fields,
repeated fields, and values outside their type or bound are all refused, each
with its own error code, and never corrected.

### Verbs, peripheral to appliance

| Verb | Fields | Sent when |
|---|---|---|
| `HELLO` | `version` integer 1 to 65535; `peripheral` token of up to 32 characters from `a-z0-9-`; `locked` boolean | on attach and on peripheral restart |
| `BUTTON` | `event` one of the event set; `foregrounded` boolean; `unlocked` boolean | on a transmitted button press |
| `STATE` | `locked` boolean | when the screen is locked or unlocked locally |

Booleans are written `0` or `1`.

### Verb, appliance to peripheral

| Verb | Fields | Sent when |
|---|---|---|
| `DISPLAY` | `status` one of the status set; `page` one of `NONE`, `WIFI`, `GUEST`; `payload` text, empty unless a page is named; `delivered` integer 0 to 9999; `error` one of the error set, `NONE` when there is none | after a successful `HELLO`, then on every change to any field |

One record carries the whole visible state and replaces the previous one
outright (specification 2.8, extension 3.2). The payload is the value for the
current page only: the Wi-Fi code payload for `WIFI`, the gallery address for
`GUEST`, from which the peripheral derives both its QR and its NFC record.

### Handshake

1. The peripheral sends `HELLO`.
2. The appliance validates the version against its supported set (initially
   `{1}`, exact match) and answers a `DISPLAY` carrying the full current
   state, which is the acceptance. A version it does not support is answered
   with a `DISPLAY` whose `error` is `BAD_VERSION` and whose other fields are
   `READY`, `NONE`, empty and `0`.
3. Only then does the appliance act on `BUTTON`. A `BUTTON` or `STATE` before
   `HELLO` is answered with `error` `NO_HELLO` and discarded.
4. A second `HELLO` on an open link is a peripheral restart: the appliance
   discards link state and answers as in step 2.

### Events

`CENTER_SHORT`, `CENTER_LONG`, `LEFT_SHORT`, `RIGHT_SHORT`. `BACK_SHORT` was
dropped at promotion (seam Q13, 2026-09-12): a short back press has no appliance
meaning, so the peripheral reports nothing for it and a long back press exits
the application locally. The wire spelling of the remaining events is fixed
here.

### Status codes

`READY`, `PRESENTING`, `GUEST_CONNECTED`, `TERMINATING`, `RECOVERY_REQUIRED`,
as extension 3.2 defines them.

### Error codes

Every code is at most eleven characters, because that is what fits the
display's error band beside a code (evaluation log 4.4). Each corresponds to
one category in the seam document's proposal, named in the last column so
the promotion review can rename knowingly.

| Code | Meaning | Seam 4.5 name |
|---|---|---|
| `NONE` | no error, the value on every record not answering a rejection | `NONE` |
| `BAD_VERSION` | `HELLO` named a version the appliance does not support | `VERSION_UNSUPPORTED` |
| `NO_HELLO` | a message other than `HELLO` arrived before a successful handshake | `HANDSHAKE_REQUIRED` |
| `MALFORMED` | the line could not be parsed: a bad escape, a byte outside printable ASCII, a missing separator | `MALFORMED` |
| `BAD_VERB` | parsed, verb not in the set | `UNKNOWN_VERB` |
| `BAD_FIELD` | parsed, a field not in the verb's set, or a field repeated | `UNKNOWN_FIELD` |
| `NO_FIELD` | parsed, a required field absent | `MISSING_FIELD` |
| `BAD_VALUE` | parsed, a value outside its type or bound | `INVALID_VALUE` |
| `TOO_LONG` | the length bound was reached before a line feed | `MESSAGE_TOO_LONG` |
| `RATE_LIMIT` | the inbound rate bound was exceeded; the message was discarded | `RATE_LIMITED` |
| `GUARD` | a `BUTTON` carried a false `foregrounded` or `unlocked` | `GUARD_NOT_SATISFIED` |
| `ACTIVE` | start requested while a session is active | `SESSION_ALREADY_ACTIVE` |
| `NO_SESSION` | terminate requested while idle | `NO_ACTIVE_SESSION` |
| `BAD_STATE` | start requested while terminating or in recovery | `INVALID_SESSION_STATE` |
| `NOT_ALLOWED` | terminate refused by configuration or by `PD9` | `TERMINATION_NOT_PERMITTED` |
| `START_FAIL` | the start was accepted and failed; the dashboard has the detail | `START_FAILED` |
| `END_FAIL` | likewise for terminate | `TERMINATE_FAILED` |

The peripheral renders the code and never a sentence. Which codes the
appliance actually emits, and when, is the appliance's interpretation table
(extension 2.1) and not fixed here.

### Bounds

| Bound | Value | Reason |
|---|---|---|
| maximum message length | 512 bytes including the line feed | the payload bound plus every other field at its longest plus framing, with room, and small enough for a buffer sized once at start |
| maximum payload length | 256 bytes decoded | holds a Wi-Fi payload with a full length SSID and passphrase; anything the display cannot show is refused by the QR encoder with its own error, not by the parser |
| maximum peripheral token | 32 characters | an identifier for the dashboard, not interpreted |
| maximum delivered count | 9999 | four digits; the display caps at 999 anyway |
| maximum inbound rate | 10 per second sustained, burst 20 | presses are made by a hand; more is a faulty or hostile peer |
| consecutive malformed before link drop | 5 | one is a glitch, five in a row is a peer speaking something else |

The peripheral bounds its own outbound queue (four messages) so a burst of
presses cannot pile up, but that is a Flipper implementation constant, not a
protocol bound: the appliance never sees or honours it. It was removed from the
definition at promotion and lives in `session/remote_session.h`.

### Version policy

One integer, exact match against the appliance's supported set, which starts
as `{1}`. No negotiation. The version stays `1` throughout the provisional
period; changes are recorded below rather than numbered, and `1` is what is
frozen at `FD20`.

### What the protocol never carries

A guest access token, the session identifier, administrator credential
material, the camera FTP password, the trusted network passphrase, a
photograph or derivative, a filesystem path, a guest device identifier, a
shell fragment, or an arbitrary command (extension 3.4, 4.1). The session
Wi-Fi passphrase is carried, inside the Wi-Fi payload, because it is the
content of that payload and has no other purpose.

## Facts from the hardware that shaped this draft

- The error band beside a code holds about eleven upper case characters, off
  a code about twenty (evaluation log 4.4). Hence the code lengths.
- A Wi-Fi payload over 53 bytes cannot be shown as a QR on this display,
  and StopBath's real payload is exactly 53 (evaluation log 4.4). The
  payload bound is deliberately not the display ceiling: the parser accepts
  what the appliance may legitimately send and the encoder refuses what
  cannot be shown, with a distinct error, so the two failures are
  distinguishable.
- The gallery address only fits a version 1 code in upper case
  (`HTTP://192.168.72.1/`), which is the same address by RFC 3986. That the
  appliance send it so is raised in the StopBath repository at promotion
  (`FD18`), not decided here.

## Change record

Required by specification 2.9 for every change made before the freeze.

| Date | Change | Reason |
|---|---|---|
| 2026-09-11 | First draft, after `FE1`, `FE2` and the surface experiments. Shape taken from the seam document's Part 4 (verbs, fields, bounds, version policy, framing with percent encoding); error codes shortened to eleven characters. | The seam's names do not fit the display beside a code; everything else the appliance asked for is honoured so promotion has little to reconcile. |
| 2026-09-12 | Promotion (`FD20`): `BACK_SHORT` removed from the event set (seam Q13, "drop BACK_SHORT"), and `peripheral_outbound_queue_depth` removed from the bounds and made a Flipper implementation constant. | Applying the two removals the author took at promotion. `BACK_SHORT` had no appliance meaning and the appliance already refuses it; the queue depth is the peripheral's own and the appliance never honours it. The appliance repository owns the definition from promotion on. |
