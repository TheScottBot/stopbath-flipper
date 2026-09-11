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
| `FE1` | launches on the author's Flipper, draws, reports every press in the control table; free heap read from the screen | PARTLY CLEARED 2026-09-11 by the author: installed and launched with `py -3 -m ufbt launch`, drew, and showed each button press on screen. Still to confirm: lock and unlock, hold Back to exit, and the on-screen free heap figure. The FE1 debug screen has since been replaced by the FE2 screens; the heap figure will return in the FE3 diagnostic view. |
| `FE2` | every display state legible on the device in daylight at arm's length | NOT YET CLEARED. The application cycles through all fourteen fixtures every four seconds. |

## Measurements owed

| Measurement | Owed to | Status |
|---|---|---|
| free heap with the application running | evaluation log 4.1 | unmeasured; shown on the `FE1` screen |
| stack actually used, via the CLI `top` command | `application.fam` `stack_size` | unmeasured; set to 4096 bytes since FE2 composes the screen on the application thread |
| total USB draw with the guest radio adapter and the Flipper both active | `FD10`, extension 5.3 | unmeasured |
| QR scanning across a representative set of phones | `FD11`, 4.4 | not started |
| daylight readability at arm's length | `FE2` gate, 4.4 | not started |
