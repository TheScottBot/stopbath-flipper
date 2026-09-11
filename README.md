# StopBath Remote

A Flipper Zero application that acts as a small physical control surface for a
StopBath appliance, so the photographer can present, start, and terminate a
session without taking a phone out.

The Flipper is a peripheral. It reports what physically happened and renders
what it is sent. StopBath remains authoritative for every session, every guest,
and the meaning of every button. The specification is
`STOPBATH_FLIPPER_SPEC.md`; the appliance side is specified in
`docs/PERIPHERAL_EXTENSION.md` in the StopBath repository.

## Status

Phases `FE1` and `FE2` are cleared, and both guest facing surfaces have been
proven on hardware ahead of their phases: the application renders every
display state from a fixture, draws a real QR code for the page's payload,
and presents the same payload over NFC as a Type 4 Tag, all confirmed with a
phone. It talks to no appliance yet. The protocol is drafted next, in `FE3`;
see `PROTOCOL.md`.

Hardware gates cleared by the author are recorded in
`HARDWARE_COMPATIBILITY.md`. Nothing in this repository claims a hardware gate
has passed.

## Supported firmware

Unleashed `unlshd-086`, API `87.6`, firmware commit `e7e4e179`. No other
distribution or version is claimed. The evidence is in
`docs/evaluation/ACTUAL_CONTRACT_EVALUATION.md`.

## Building the application

The build tool is `ufbt`, pinned to the Unleashed SDK for the version above.
The SDK is kept per project under `.ufbt/` (ignored by git) and selected by the
tracked `.env` file.

```bash
py -3 -m pip install --user ufbt==0.2.6
```

```bash
py -3 -m ufbt update --hw-target f7 --url "https://github.com/DarkFlippers/unleashed-firmware/releases/download/unlshd-086/flipper-z-f7-sdk-unlshd-086.zip"
```

```bash
py -3 -m ufbt
```

The first `ufbt` run downloads the pinned ARM toolchain (`gcc-arm-none-eabi
12.3`, package 39) into `~/.ufbt/toolchain` and links it into `.ufbt/`. The
result is `dist/stopbath_remote.fap`. On Linux and in continuous integration
replace `py -3 -m ufbt` with `ufbt`.

To install onto an attached Flipper and launch it:

```bash
py -3 -m ufbt launch
```

## Host tests

The pure logic under `remote_input/` has no SDK dependency and is tested on the
development machine with `gcc` and `make`:

```bash
make test
```

The sanitiser build needs a compiler with `libasan` and `libubsan`, which the
MinGW `gcc` on Windows does not ship. Run it under WSL or on Linux:

```bash
make test-sanitise
```

The font metrics table used for text truncation is generated from the pinned
firmware's font data and checked in. To regenerate it, check the firmware out
at the pinned tag beside this repository and point the generator at its font
source:

```bash
git clone --depth 1 --branch unlshd-086 https://github.com/DarkFlippers/unleashed-firmware.git ../unleashed-firmware
```

```bash
py -3 scripts/generate_font_metrics.py ../unleashed-firmware/lib/u8g2/u8g2_fonts.c
```

The typography scan required by specification 0.8:

```bash
make check-typography PYTHON="py -3"
```

## Layout

| Path | Contents |
|---|---|
| `application.fam` | the application manifest |
| `stopbath_remote.c` | the SDK facing application, kept thin |
| `remote_input/` | pure logic: what a press does to the device, no SDK |
| `remote_display/` | pure logic: what goes where on the screen, the shared display fixtures, the generated font metrics, the QR wrapper, and the NDEF builder, no SDK |
| `lib/qrcodegen/` | the vendored QR encoder, unmodified, with its licence and provenance |
| `tests/` | host tests and the shared test harness |
| `scripts/` | repository checks |
| `docs/evaluation/` | the Plan stage evidence log |

## Documents

`PROTOCOL.md`, `TESTING.md`, `IMPLEMENTATION_DEVIATIONS.md`,
`HARDWARE_COMPATIBILITY.md`, `FIELD_NOTES.md`, and
`docs/evaluation/ACTUAL_CONTRACT_EVALUATION.md` are the documents the
specification requires. Each says what it is for at the top.

## Licence

MIT, matching the StopBath project. See `LICENSE`.
