# Testing

Tests cover behaviour, not implementation trivia (specification Part 6). What
is automated runs on a development machine with no Flipper attached and no SDK
present. What cannot be automated is listed here as a hardware gate or as a
testing deviation with its reason, never left unstated.

## Automated

| Suite | Runs where | Command |
|---|---|---|
| `tests/test_remote_input_model.c` (FE1) | host, any `gcc` | `make test` |
| `tests/test_remote_display_layout.c` (FE2) | host, any `gcc` | `make test` |
| `tests/test_remote_font_metrics.c` (FE2) | host, any `gcc` | `make test` |
| `tests/test_remote_qr.c` (FE5, brought forward for the FD4 experiment) | host, any `gcc` | `make test` |
| `tests/test_remote_ndef.c` (FE6, brought forward for the FD15 to FD17 experiment) | host, any `gcc` | `make test` |
| `tests/test_remote_protocol.c` (FE3) | host, any `gcc` | `make test` |
| `tests/test_development_peer.c` (FE3) | host, any `gcc` | `make test` |
| `tests/test_remote_session.c` (FE4) | host, any `gcc` | `make test` |
| `tests/test_link_integration.c` (FE4) | host, any `gcc` | `make test` |
| protocol parser fuzz harness (FE3) | host, deterministic; sanitised on Linux | `make fuzz`, `make fuzz-sanitise` |
| generated tables match `protocol.json` (FE3) | any Python 3 | `make check-protocol-tables` |
| all of the above under address and undefined behaviour sanitisers | Linux or WSL | `make test-sanitise` |
| typography scan (specification 0.8) | any Python 3 | `make check-typography` |
| application build, warnings as errors, against the pinned SDK | host with `ufbt` | `py -3 -m ufbt` |

Every unit test is a table driven C function run by the shared harness in
`tests/test_support.h`. A test case with no assertions fails; an empty test
proves nothing.

Continuous integration (`.github/workflows/ci.yml`) runs all four on every
pull request and on every push to `main`.

## What each phase tests first

`FE1`: button to local action mapping, lock state transitions, unknown input
ignored rather than crashing. All three are in `tests/test_remote_input_model.c`
and were written and seen to fail before the model was implemented.

`FE2`: state to layout mapping for every state, unknown status renders a safe
fallback, long values truncate rather than overflow. All in
`tests/test_remote_display_layout.c`, against the shared fixtures in
`remote_display/remote_display_fixtures.c`, written and seen to fail (17 of 18
cases) before the layout was implemented. Truncation is measured with the
firmware's own glyph advances, generated into
`remote_display/remote_font_metrics.c` by `scripts/generate_font_metrics.py`
and pinned by `tests/test_remote_font_metrics.c`, so what the host says fits
is what the device draws.

FD4 experiment (2026-09-11, ahead of `FE5`): version selection for the two
experiment payloads, finder patterns at the corners, the version 3 byte
capacity exactly at and one over the bound, empty payload refused, and the
two pixel per module bitmap placement, in `tests/test_remote_qr.c`, written
and seen to fail (5 of 9 cases) before the wrapper was implemented. NOT yet
done from `FE5`: known payloads against published full matrix vectors, and
the distinct error the display shows for a refused payload.

FD15 to FD17 experiment (2026-09-11, ahead of `FE6`): full byte vectors for
the URI record of the gallery address and the Wi-Fi credential record of an
example payload, unescaping of the grammar's reserved characters, field
order independence, refusal of every malformed or unsupported payload, and
the largest credential fitting the buffer, in `tests/test_remote_ndef.c`,
written and seen to fail (2 of 8 cases passing against a stub, both
negative) before the builder was implemented. The vectors were written from
the firmware's NDEF parser and Android's parser, not from the NFC Forum or
Wi-Fi Alliance specifications; `FE6` proper owes vectors from a published
source.

`FE3`: every verb round trips through the encoder and the parser; truncation,
overlong lines, unknown verbs and fields, missing and repeated fields, wrong
version, embedded nulls, non printable bytes, bad escapes, and payloads at and
one over the bound are each refused with their typed code; the assembler
recovers after every error; nothing in the parse or encode path allocates
(proven by wrapped heap functions); and the development peer drives every
display state, records every event, answers the handshake, and produces each
misbehaviour mode. All written and seen to fail before the library existed.
The parser is fuzzed with a deterministic driver whose oracle re-encodes and
re-parses every accepted message; it runs under the sanitisers in continuous
integration.

`FE4`: handshake success, version mismatch rejection, disconnect during a
message, reconnect discards local state, button events not queued across a
disconnection, an event not emitted while the guard is unsatisfied, and the
sensitive payload cleared on disconnect. In `tests/test_remote_session.c`,
written and seen to fail before the session existed. `tests/test_link_integration.c`
wires the real session to the real development peer over a byte pipe and runs
the connect and disconnect cycle twenty times in software, the mirror of the
hardware gate; it asserts reconnect always yields the peer's current state and
no press crosses a gap.

Later phases add their own suites and are listed here when they do.

## Hardware gates

Cleared by the author on the real device and recorded in
`HARDWARE_COMPATIBILITY.md`. Never claimed here, never inferred from a green
automated run.

`FE1`: the application launches on the author's Flipper, draws, and reports
every press in the control table of specification 2.3. The free heap figure
shown on screen is the number the evaluation log lists as unmeasured.

`FE2`: each of the fourteen fixtures, which the application cycles through
every four seconds, is legible on the device in daylight at arm's length. The
lock band appears on a short Down press over whichever fixture is showing.

FD4 experiment: the Wi-Fi fixture (the credentials in the untracked
`experiment_credentials.h`, version 3 at 58 pixels with no quiet zone inside
the area) and the gallery fixture
(`HTTP://192.168.72.1/`, version 1 at 42 pixels) each scan from a phone at
arm's length. Record the handset, the app used to scan, distance, and
lighting in `HARDWARE_COMPATIBILITY.md`.

`FE4`: with the development peer running on a host and the Flipper plugged in,
pull and reinsert the cable twenty times and restart each side independently,
with no repair step, and confirm the display always returns to the peer's
current state. The same check against a real appliance is `PE2` in the
extension document and must not be claimed here.

FD15 to FD17 experiment: while a code page is showing, an Android phone held
to the back of the device (the NFC antenna is there) offers to join the
network from the Wi-Fi page, and both an Android phone and an iPhone open the
gallery address from the gallery page. The code must remain drawn throughout.
Record handset, operating system version, and which of the three happened.

## Testing deviations

- The sanitiser build cannot run under the MinGW `gcc` on the Windows
  development host, which ships neither `libasan` nor `libubsan` (measured
  2026-09-11: the link fails on `-lubsan`). It runs under WSL and in
  continuous integration on Linux. Specification 0.10 asks for the sanitiser
  on the development machine; WSL on the same machine is how that is met, and
  it was run there clean on 2026-09-11.
- The USB transport in `remote_transport.c` is not unit tested: it is the
  SDK edge (USB CDC channel, DTR, suspend and wakeup) and needs the device.
  It holds no logic of its own; every decision is in `remote_session`, which
  is fully tested, and the integration test drives the same session against
  the real peer. The transport is exercised at the FE4 hardware gate.
- The application's foreground guard always reports foregrounded, because a
  Flipper FAP has no background state to detect: the loader runs one
  application, and when the desktop locks, input stops reaching the viewport
  entirely (the FD19 finding). Recorded in `IMPLEMENTATION_DEVIATIONS.md` and
  the evaluation log. Display-off is likewise not separately detected; the
  lock is the operative guard for the pocket case (2.4).
- The vendored encoder under `lib/qrcodegen/` is compiled on the host with
  the SDK's warning set rather than this project's stricter set, because
  Linux gcc 15 flags `-Wconversion` inside it and the file is not edited
  (see `IMPLEMENTATION_DEVIATIONS.md`). On the device it is compiled under
  the SDK's set, as everything is, and is clean.
- The SDK facing glue in `stopbath_remote.c` is not unit tested. It is a
  translation between firmware enumerations and the model's, plus resource
  acquisition and release, and exercising it requires the firmware. The
  translation is written as exhaustive switches so the compiler's `-Werror`
  catches a missing case; the behaviour behind it is fully tested in the model.
