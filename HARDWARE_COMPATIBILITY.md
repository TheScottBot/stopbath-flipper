# Hardware compatibility

What this application has been run on, by whom, and what was observed. Only
the author adds a row that says a gate was cleared. Anything measured on the
device is recorded here with the date; anything not yet measured says so.

## Supported device and firmware

| Item | Value | Source |
|---|---|---|
| Device | Flipper Zero, hardware target `f7` | the SDK's `components.json` |
| Firmware distribution | Unleashed | author, 2026-09-11 (`FD12`) |
| Firmware release | `unlshd-086`, built 2026-03-08 | device version screen |
| Firmware commit | `e7e4e179be8ff969925760597e16fdb7545e6c14` | `git ls-remote --tags` against the Unleashed repository |
| API version | `87.6` | `targets/f7/api_symbols.csv` line 2 at that commit, and the deployed SDK |
| Co-processor firmware | `1.20.0` | device version screen, `fbt_options.py` line 25 |
| SD card application pack variant | not yet read from the device | the author reads it from the Apps menu; it does not affect the API |
| Toolchain | `gcc-arm-none-eabi 12.3`, Flipper package 39 | `scripts/toolchain/fbtenv.sh` at that commit |
| Build tool | `ufbt 0.2.6` with the release SDK zip | `FD1` |
| Device name | `Arbuntal`, reported by `ufbt launch` as `FLIP_ARBUNTAL` on `COM5` | author's host, 2026-09-11; the CLI channel of the device |
| Installed path on the device | `/ext/apps/Tools/stopbath_remote.fap` | `ufbt launch`, from `fap_category` in the manifest |

## Hardware gates

| Phase | Gate | Status |
|---|---|---|
| `FE1` | launches on the author's Flipper, draws, reports every press in the control table; free heap read from the screen | CLEARED 2026-09-11 by the author: installed and launched with `py -3 -m ufbt launch`, drew, and showed each button press on screen; the lock and the exit were exercised through the FE2 and experiment sessions the same day. The free heap figure was not noted before the FE1 debug screen was replaced; it returns in the FE3 diagnostic view. |
| `FE2` | every display state legible on the device in daylight at arm's length | CLEARED 2026-09-11 by the author ("looks good"), with one amendment made on the author's observation: the header sat inside the code area and was moved into the column. Daylight specifically was not stated; indoor legibility was. |
| `FE4` | against the development peer, twenty cable pulls and each side restarted, with no repair step; the display always returns to the peer's current state | NOT YET CLEARED. Run the peer (`make peer`, then `build/host/development_peer` against the Flipper's second serial node) with the Flipper running the application. The software mirror passes in `tests/test_link_integration.c`. |
| FD4 experiment | the Wi-Fi code (version 3, 58 px, no inner quiet zone) and the gallery code (version 1, 42 px) each scan from a phone | CLEARED 2026-09-11 by the author: the version 3 Wi-Fi code scanned from about 5 cm on a Samsung Galaxy Z Fold3 (the StopBath repository's `IMPLEMENTATION_DEVIATIONS.md` records the author's Fold as `SM-F926B` on Android 15, measured 2026-09-04). Not recorded: scanning app, lighting, and whether the version 1 gallery code was scanned separately. |
| FD15 to FD17 experiment | an Android phone held to the device offers to join from the Wi-Fi page; Android and iPhone open the gallery from the gallery page; the code stays drawn meanwhile | ATTEMPT 1, 2026-09-11, FAILED: with the minimal one byte ATS, an Android phone read nothing on either page over a whole cycle. ATTEMPT 2, 2026-09-11, WORKED, reported by the author: with the spelled out ATS (TL 5, T0 78, TA1 80, TB1 80, TC1 02; UID seven bytes, ATQA 44 00, SAK 20) the Android phone read the Wi-Fi credential record from the Wi-Fi page while the code was on screen, joined the network, and on the gallery page read the URI record and opened the address. Handset: Samsung Galaxy Z Fold3 (the StopBath repository's `IMPLEMENTATION_DEVIATIONS.md` records the author's Fold as `SM-F926B` on Android 15, measured 2026-09-04). Not recorded: whether an iPhone was tried. |

## Measurements owed

| Measurement | Owed to | Status |
|---|---|---|
| free heap with the application running | evaluation log 4.1 | unmeasured; shown on the `FE1` screen |
| stack actually used, via the CLI `top` command | `application.fam` `stack_size` | unmeasured; set to 4096 bytes since FE2 composes the screen on the application thread |
| free heap with the link open and a code shown | evaluation log 4.1 | unmeasured; read with the CLI `free` command over channel 0, which stays available |
| which serial node is the application's channel 1 | evaluation log 4.2, FE4 gate | unverified; identified on the host with the nodes listed before and after attach |
| total USB draw with the guest radio adapter and the Flipper both active | `FD10`, extension 5.3 | unmeasured |
| QR scanning across a representative set of phones | `FD11`, 4.4 | not started |
| daylight readability at arm's length | `FE2` gate, 4.4 | judged indoors by the author 2026-09-11; daylight still owed |
| an iPhone against both pages | `FE6` gate, `FD11` | not yet tried |
