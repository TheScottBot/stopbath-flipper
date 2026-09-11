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

## Testing deviations

- The sanitiser build cannot run under the MinGW `gcc` on the Windows
  development host, which ships neither `libasan` nor `libubsan` (measured
  2026-09-11: the link fails on `-lubsan`). It runs under WSL and in
  continuous integration on Linux. Specification 0.10 asks for the sanitiser
  on the development machine; WSL on the same machine is how that is met, and
  it was run there clean on 2026-09-11.
- The SDK facing glue in `stopbath_remote.c` is not unit tested. It is a
  translation between firmware enumerations and the model's, plus resource
  acquisition and release, and exercising it requires the firmware. The
  translation is written as exhaustive switches so the compiler's `-Werror`
  catches a missing case; the behaviour behind it is fully tested in the model.
