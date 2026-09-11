# Actual contract evaluation

This is the Plan stage evidence log required by Part 4 of
`STOPBATH_FLIPPER_SPEC.md`. It records, for every firmware capability, transport,
and dependency this application depends on: the source, the exact version or
commit, the retrieval date, the files read, what was actually observed, remaining
uncertainties, and which author decisions are still open.

## Provenance rules this document obeys

- No claim about the firmware is entered here unless the real source, header, or
  official document was read at the pinned commit (spec 0.2).
- Every claim carries a file path and, where it matters, a line number, so it can
  be re-read against the same commit.
- A section marked HARDWARE has not been measured because it needs the real device
  in the author's hands. It is not a placeholder for a guess (spec 4.5, V1).
- No em dash appears in this file (spec 0.8). Hyphens, commas, colons, and
  parentheses only. Markdown table separator rows and command line flags are the
  only places a double hyphen appears, and the scan built in FE1 must exempt
  exactly those and nothing else.

## Status summary (last updated 2026-09-11, after the FE4 hardware gate cleared)

| Section | Topic | Status |
|---|---|---|
| Host | Development host toolchain | Observed 2026-09-11 |
| 4.1 | Firmware and SDK | READ at the pinned commit. Distribution, version, API version, toolchain, build system, warning flags, application model, and the autostart question are all answered from source. Heap available to the application is HARDWARE. |
| 4.2 | Transport | READ. Three candidates enumerated from source with exports confirmed. Recommendation recorded for `FD2`. Host side enumeration and reconnection behaviour are HARDWARE and belong to Part 6 of the extension document. |
| 4.3 | Power | Charge suppression API confirmed from source. Total draw is HARDWARE and unmeasured. |
| 4.4 | Display and QR | Display geometry and drawing primitives confirmed from source. Rendering time, memory, daylight readability and scanning are HARDWARE. QR encoder candidate identified, not yet accepted. |
| NFC | `FD15`, `FD16`, `FD17` | Runtime NDEF presentation has a real API surface (Type 4 Tag listener). All three decisions remain HARDWARE. |
| Lock | `FD19` | ANSWERED from source: the firmware lock is not reachable from an application. An application level lock is required. |
| FE1 | Toolchain proof | DONE 2026-09-11. `ufbt 0.2.6` deploys the Unleashed SDK zip directly; the application builds warning clean at API 87.6; the host tests pass on MinGW and under the sanitisers in WSL. Hardware gate partly cleared by the author the same day. |
| FE2 | Display | DONE 2026-09-11. Every display state composes from a fixture on the host; font metrics measured from the firmware's font data. Gate cleared by the author the same day, indoors. |
| Experiments | `FD4`, `FD15` to `FD17` | DONE 2026-09-11, brought forward from FE5 and FE6 at the author's direction so the two guest facing surfaces were proven on hardware before any protocol or appliance work. Both surfaces work. Recorded as a build order deviation in `IMPLEMENTATION_DEVIATIONS.md`. |
| FE3 | Protocol, peer, fuzz | DONE 2026-09-11 on the automated side. Provisional protocol drafted in `PROTOCOL.md` and `protocol.json`; parser and encoder generated from one table; parser fuzzed clean; development peer with misbehaviour modes and a serial shell. Decisions `FD7`, `FD8`, `FD9` and the framing settled by the author for the provisional period; frozen at `FD20`. No hardware gate: FE3 touches no device. |
| FE4 | Transport, session | DONE 2026-09-11, hardware gate cleared the same day. USB CDC dual mode, channel 1, from `usb_uart_bridge.c` and `furi_hal_usb_cdc.h` at the pinned commit (4.2). The session state machine (handshake, reconnection, the guard, no queueing across a disconnect, sensitive payload cleared on drop) is in `session/remote_session.c`, fully host tested; the integration test runs the connect and disconnect cycle twenty times against the real peer. FAP builds clean. On the device against the Pi: handshake, all four reported buttons, both lock transitions, and reconnection after a cable pull all confirmed. Five bugs surfaced only at the gate and are recorded under "Hardware findings, FE4" below; none was visible to the host tests, because each lived in the SDK glue or the peer's OS I/O, neither of which is host tested. |

## Host, development machine (observed 2026-09-11)

| Tool | Observed | Notes |
|---|---|---|
| Operating system | Windows 11 Home 10.0.26200 | From the session environment. |
| Processor architecture | ARM64 | Established 2026-09-11: the Python build is `Python314-arm64` and WSL reports `aarch64`. The Git Bash shell reports `AMD64` because it runs under x64 emulation. The Windows Flipper toolchain is x86_64 only and runs under the same emulation. |
| C compiler | `gcc.exe (GCC) 15.2.0` at `C:\Users\sptip\scoop\apps\gcc\current\bin\gcc.exe` | A MinGW x86_64 build. MEASURED 2026-09-11: `-fsanitize=address,undefined` fails to link on `-lubsan`, so the sanitiser build cannot run here. It compiles the pure logic and runs the host tests. |
| make | GNU Make 4.4.1 | Installed via scoop on 2026-09-11 for the host tests. |
| clang, cl | absent | `where.exe clang cl` found neither. |
| Python | 3.14.7 via the `py` launcher | The bare `python` command resolves to the Microsoft Store stub and does not run. |
| ufbt | 0.2.6 | Installed 2026-09-11 with `py -3 -m pip install --user ufbt`. Its `--help` auto-deployed the official 1.4.3 SDK into `~/.ufbt`, which this repository does not use; the per-project checkout under `.ufbt/` is the Unleashed one, see 4.1. |
| WSL | WSL 2, Ubuntu 26.04 LTS, `aarch64`, `gcc 15.2.0`, GNU Make 4.4.1 | The author installed `build-essential` on 2026-09-11. The sanitiser build (`make test-sanitise`, address and undefined behaviour, no recovery) ran there the same day: 12 of 12 cases pass. This is the development machine path spec 0.10 requires; continuous integration runs the same target on Linux. |
| git | present | Used read-only throughout, per spec 0.1. |

Consequence, confirmed at FE1: the pure logic builds with the host MinGW compiler
for ordinary tests, and the sanitiser build has a Linux path (WSL locally, once
`build-essential` is installed there, and `ubuntu-latest` in continuous
integration). `Makefile` carries both targets.

Toolchain availability for every relevant platform, checked 2026-09-11 with a
HEAD request against the URL pattern in `scripts/toolchain/fbtenv.sh` and
`scripts/toolchain/windows-toolchain-download.ps1` at the pinned commit:

| Platform | Package | Response |
|---|---|---|
| `x86_64-windows` | `gcc-arm-none-eabi-12.3-x86_64-windows-flipper-39.zip` | 200 |
| `x86_64-linux` | `gcc-arm-none-eabi-12.3-x86_64-linux-flipper-39.tar.gz` | 200 |
| `aarch64-linux` | `gcc-arm-none-eabi-12.3-aarch64-linux-flipper-39.tar.gz` | 200 |

So the Windows host builds under emulation, WSL on this machine could build
natively, and `ubuntu-latest` builds natively.

## 4.1 Firmware and SDK

### Device and distribution (author supplied, 2026-09-11)

The version string displayed on the author's device:

    unlshd-086 [08-03-2026] e7e4e179 [87.6] 1.20.0:L[L] release-cfw

Read against the source below, the fields are: Unleashed release tag `unlshd-086`;
build date 8 March 2026; git commit `e7e4e179`; API version `87.6`; co-processor
firmware `1.20.0`; the remaining fields are build target and branch markers that
this evaluation does not depend on.

`FD12` is therefore answered by the author as Unleashed. The official firmware is
NOT a supported target for this application unless the author later says so. The
two distributions share the application build system and most of the API surface,
but this document makes no claim about the official firmware at any version.

### Source (retrieved 2026-09-11)

| Item | Value |
|---|---|
| Repository | `https://github.com/DarkFlippers/unleashed-firmware` |
| Existence confirmed by | `git ls-remote --tags` before cloning (spec 0.2) |
| Tag | `unlshd-086` |
| Commit | `e7e4e179be8ff969925760597e16fdb7545e6c14`, committed 2026-03-08 21:02:17 +0300 |
| Match to device | The first eight characters equal the commit on the device and the commit date equals the build date on the device. |
| Clone | shallow, depth 1, read-only, in the session scratchpad outside this repository |
| Submodules | `assets/protobuf` fetched at its pin `1c84fa48919cbb71d1cc65236fc0ee36740e24c6` (dated 2024-12-17). No other submodule fetched. |
| Release | `https://github.com/DarkFlippers/unleashed-firmware/releases/tag/unlshd-086`, published 2026-03-08T18:19:34Z |
| SDK artefact | `flipper-z-f7-sdk-unlshd-086.zip` in that release |

The release carries three firmware update packages: `flipper-z-f7-update-unlshd-086`,
`unlshd-086c` and `unlshd-086e`. `CHANGELOG.md:56-66` at the pinned commit
defines the suffix: the package contains the updater, assets, and the firmware,
and the suffix says only which application packs from
`https://github.com/xMasterX/all-the-plugins` are included on the SD card (no
suffix: base pack; `c`: none; `e`: base and extra). The firmware and API are the
same in all three, so the variant does not bear on anything in this document. It
is recorded in `HARDWARE_COMPATIBILITY.md` as an SD card fact once the author
reads it from the Apps menu.

### Observed from source

Firmware identity and versions:

- `fbt_options.py:6` `FIRMWARE_ORIGIN = "Unleashed"`.
- `fbt_options.py:25` `COPRO_CUBE_VERSION = "1.20.0"`, matching the `1.20.0` on
  the device.
- `targets/f7/api_symbols.csv:2` `Version,+,87.6,,`, matching the `[87.6]` on the
  device. This file is the whole exported API: a function or variable marked `+`
  is callable from an application; one marked `-` is not, even if a header declares
  it. Every export claim below was checked against this file.

Toolchain:

- `scripts/toolchain/fbtenv.sh:7` pins the packaged toolchain as
  `FBT_TOOLCHAIN_VERSION=39`, and line 153 names it
  `gcc-arm-none-eabi-12.3-<arch>-<os>-flipper-39.tar.gz`, downloaded from
  `https://update.flipperzero.one/builds/toolchain/`. The angle brackets there
  are the firmware's own naming pattern quoted from its script, not a placeholder
  of this document's.
- `fbt_options.py:42` accepts host compilers whose version string contains
  ` 12.3.` or ` 13.2.`.

Warning flags, which settle what "the strictest setting the toolchain and SDK
permit" in spec 0.10 means for the application build (`site_scons/cc.scons:4-40`):
`-std=gnu2x -Wstrict-prototypes` for C, and for all C and C++:
`-Wall -Wextra -Werror -Wno-error=deprecated-declarations
-Wno-address-of-packed-member -Wredundant-decls -Wdouble-promotion -Wundef`, with
`-mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16 -mthumb`. Warnings are already
errors in the firmware build system. The freestanding protocol library in FE1
should compile under at least this set on the host compiler as well.

Application model (`documentation/AppManifests.md`, `documentation/AppsOnSDCard.md`,
both read in full):

- An application is declared in an `application.fam` manifest with
  `apptype=FlipperAppType.EXTERNAL`, built by `./fbt fap_<appid>` in tree, or
  launched onto a connected device with `./fbt launch APPSRC=<path>`. (Both
  angle bracket forms are quoted from the firmware documentation.)
- A FAP is an ELF loaded from the SD card into RAM by the loader. Only code and
  data sections occupy RAM. The loader checks that the application's MAJOR API
  version matches the firmware's; the minor version may differ.
- Manifest fields relevant to this project: `stack_size`, `fap_private_libs` (a
  library built from sources under the application's `lib` folder, which is how the
  QR encoder will be carried), `fap_libs`, `fap_version`, `fap_category`,
  `fap_extbuild`.
- `sdk_headers` and the symbol table: an application can only call what
  `api_symbols.csv` marks `+`.

API mismatch handling, an Unleashed specific behaviour worth recording
(`applications/services/loader/loader.c:717-724`): if starting a FAP fails with
`LoaderStatusErrorApiMismatch`, the loader immediately retries with the mismatch
check IGNORED. The consequence is that a FAP built against a different major API
version will be loaded and run rather than refused. Spec 0.11 and 2.10 want an
incompatible peer rejected explicitly. That protection therefore cannot be left to
the loader on this distribution; the peripheral protocol version check (`HELLO`)
is the only version check that reliably refuses.

Memory (`targets/f7/stm32wb55xx_flash.ld:10` and `:104-112`,
`targets/f7/inc/FreeRTOSConfig.h:36`, `furi/core/memmgr_heap.c:101`): RAM1 is
`0x2FFF8` bytes, a little under 192 KB. The heap is everything in RAM1 after the
firmware's own `.data` and `.bss` up to the stack. Its size is therefore a property
of this firmware build and is not a constant in source. `memmgr_get_free_heap`,
`memmgr_get_total_heap`, `memmgr_get_minimum_free_heap` and
`memmgr_heap_get_max_free_block` are exported (`api_symbols.csv:2706-2711`), so the
application can report its own headroom in the diagnostic view spec 2.11 requires.
The number available to this application is HARDWARE: read it on the device with
the `free` command over the CLI, with the application running and a QR displayed.

Autostart (`applications/services/loader/loader.c:885-888`,
`scripts/fbt_tools/fbt_apps.py:82`): the only firmware autostart is
`FLIPPER_AUTORUN_APP_NAME`, a constant compiled into the firmware from the build
configuration. It is not available to a FAP without rebuilding the firmware, which
is out of scope. The application must be started either by hand from the menu or
by the host, and the host has two ways to do it, both read from source:

- the CLI `loader open` command taking a name or path
  (`applications/services/loader/loader_cli.c:129-135`, which also provides
  `list`, `info`, and `close`);
- the RPC `App.StartRequest` message, see 4.2 candidate B.

`loader_start` resolves its `name` argument first against internal applications,
then against the external application registry, then as a path on the SD card
(`loader.c:706-724`), so a path to the FAP works for both.

Input (`applications/services/input/input.c:14-15, 38-52, 140-160`): a press starts
a 150 ms timer; `InputTypeLong` is published on the second tick, 300 ms after the
press; `InputTypeShort` is published on release only if release came before that.
`InputTypeRepeat` follows every 150 ms while held. These constants are compiled
into the firmware and are not configurable by an application. An application
wanting a longer deliberate hold (the main specification's `D20` guidance is about
one second for the dashboard) must implement it from `InputTypePress`,
`InputTypeRelease` and its own timer. Which duration the Flipper uses is an
Appendix A value settled in 4.4 and field use, not here.

FE4 addition, 2026-09-11: the foreground half of the guard (2.4) has no
firmware signal on a FAP, because the loader runs one application and desktop
lockdown removes input from a backgrounded viewport (the Lock section). The
application reports foregrounded true and relies on the lock as the operative
guard, recorded as a deviation in `IMPLEMENTATION_DEVIATIONS.md`. The transport
uses the DTR control line for the host opening the port and USB suspend and
wakeup for the cable, both from `furi_hal_usb_cdc.h`; the USB configuration
(dual CDC) persists across suspend, so a cable pull needs no re-open, which is
what makes reconnection require no repair step.

### Conclusions accepted

- Supported firmware: Unleashed `unlshd-086`, commit `e7e4e179be8f`, API `87.6`.
- Build: the in-tree `fbt` at that commit, or the release SDK zip with `ufbt`.
  Which of the two this repository pins is part of `FD1` below.
- Warnings as errors with the flag set above.
- The application cannot be autostarted by the firmware; the host launches it over
  the CLI or RPC, or the photographer opens it from the menu.
- The loader's API mismatch retry means the protocol's own version check is the
  only reliable rejection of an incompatible peer.

### Uncertainties and author questions

- Heap available to the application on the device (HARDWARE).
- Whether `ufbt` accepts the Unleashed SDK zip directly. VERIFIED 2026-09-11.
  `ufbt/bootstrap.py` in `ufbt 0.2.6` has a `UrlSdkLoader` (`--url`, requiring
  `--hw-target`) that fetches any zip and extracts it into the state directory.
  `py -3 -m ufbt dotenv_create` created `.env` with `UFBT_HOME=.ufbt`, then
  `py -3 -m ufbt update --hw-target f7 --url` with the release zip URL wrote
  `.ufbt/current/ufbt_state.json` recording mode `url` and version `unknown`,
  and `.ufbt/current/components.json` reads `"version": "unlshd-086"`. The
  deployed `api_symbols.csv` line 2 is `Version,+,87.6,,`. A version of
  `unknown` means every `ufbt update` re-downloads the zip; an ordinary build
  does not.
- The deployed `sdk.opts` carries the compiler flags 4.1 recorded from
  `site_scons/cc.scons`, plus `-DFW_ORIGIN_Unleashed`, `-Os`, and section
  garbage collection. The application built warning clean under them on
  2026-09-11: `dist/stopbath_remote.fap`, 4504 bytes, `Target: 7, API: 87.6`.
- The manifest `sources` glob is RECURSIVE
  (`scripts/fbt_tools/sconsrecursiveglob.py` `GlobRecursive` at the pinned
  commit; `scripts/fbt/util.py:16` `GLOB_FILE_EXCLUSION = ["*~"]` is the only
  built in exclusion). `application.fam` therefore excludes `tests`, `.ufbt`,
  `build` and `dist` by name, or the host tests and the SDK's own project
  template would be compiled into the application.
- `ufbt` regenerates `.vscode/compile_commands.json` on every build. Ignored
  by git.

## 4.2 Transport

Three candidates were found in source. For each: whether an application may claim
it, whether it survives either side restarting, how reconnection is detected, the
framing and size limits, whether it conflicts with charging or the device's own USB
behaviour, and how it is inspected while debugging.

### Candidate A: USB CDC channel claimed by the application

Evidence, all at the pinned commit:

- `targets/furi_hal_include/furi_hal_usb.h`: `furi_hal_usb_set_config(FuriHalUsbInterface*, void*)`
  switches the whole USB device between modes; `usb_cdc_single` and `usb_cdc_dual`
  are the two CDC modes. Exported: `api_symbols.csv:1841`, `:7255-7256`.
- `targets/f7/furi_hal/furi_hal_usb_cdc.h`: per channel callbacks
  (`tx_ep_callback`, `rx_ep_callback`, `state_callback`, `ctrl_line_callback`,
  `config_callback`), `furi_hal_cdc_send(if_num, buf, len)`,
  `furi_hal_cdc_receive(if_num, buf, max_len)`, `CDC_DATA_SZ 64`. Exported:
  `api_symbols.csv:1433-1437`.
- `applications/services/cli/cli_vcp.h`: `RECORD_CLI_VCP`, `cli_vcp_enable`,
  `cli_vcp_disable`. Exported: `api_symbols.csv:923-924`.
- `applications/main/gpio/usb_uart_bridge.c`: an in-tree application that does
  exactly this. `usb_uart_vcp_init` (lines 111-121) either disables the CLI and
  takes channel 0 under `usb_cdc_single`, or switches to `usb_cdc_dual`, re-enables
  the CLI on channel 0, and takes channel 1. `usb_uart_worker` (lines 296-300)
  restores `usb_cdc_single` and the CLI on exit. This is the reference for the
  acquire and release sequence and for the exit path cleanup spec 0.10 demands.

What the host sees (`targets/f7/furi_hal/furi_hal_usb_cdc.c:57-62, 432-462`):
vendor `0x0483`, product `0x5740` (the STMicroelectronics virtual COM port
identifiers, shared by the single and dual modes and by every Flipper), product
string equal to the device name, serial string `flip_` followed by the device
name. Both CDC channels sit under one USB device, so on Linux they appear as two
`ttyACM` nodes of the same device distinguished by interface number. A udev rule
on the appliance must therefore match the serial string and the interface number,
never the vendor and product pair alone. Stability of that match across
reattachment and reboot is HARDWARE and is owed to Part 6 of the extension
document, not here.

Connection detection (`furi_hal_usb_cdc.c:474-495, 529-547`,
`applications/services/cli/cli_vcp.c:116-131`): the channel's `state_callback`
receives connected on USB wakeup and disconnected on USB suspend, which is what a
cable pull produces; `ctrl_line_callback` receives the DTR bit, which is what the
host opening or closing the port produces. The CLI itself treats DTR as the
connected signal and suspend or DTR drop as disconnected. Both signals are
available to an application on its own channel. This gives the application the
two facts 2.10 needs: whether the cable is in, and whether the appliance has the
port open.

Survival across restarts: the application keeps running when the cable is pulled;
the mode switch is a property of the application's lifetime, not of the
connection. When the Flipper restarts, the application is not running until
relaunched (see autostart in 4.1), and the host sees only the CLI channel. When
the appliance restarts, the Flipper sees suspend, then wakeup, then DTR.

Framing: none. The channel is a byte stream in 64 byte USB packets. The protocol
supplies its own line framing, which is what both specifications expect ("a plain
serial line").

Charging: the same cable carries USB power; see 4.3.

Debugging: any serial terminal on the appliance side reads the channel. With
`usb_cdc_dual`, the CLI remains available on channel 0 at the same time, so
`free`, `loader`, and the log can be read while the application runs.

Conflict with the device's own USB behaviour: `furi_hal_usb_set_config` returns
false when the USB mode is locked (`furi_hal_usb.h:38-45`). The CLI's
`start_rpc_session` locks it (`applications/services/rpc/rpc_cli.c:47`), as does
the desktop lock when a PIN is set (`applications/services/desktop/desktop.c:407-411`
disables the CLI VCP). The application must check the return value and surface
failure, as spec 0.10 requires anyway.

### Candidate B: the firmware RPC application data exchange

Evidence:

- `applications/services/rpc/rpc_app.h` (read in full): `rpc_system_app_set_callback`,
  `rpc_system_app_send_started`, `rpc_system_app_send_exited`,
  `rpc_system_app_confirm`, `rpc_system_app_exchange_data(RpcAppSystem*, const uint8_t*, size_t)`,
  and the `RpcAppEventTypeDataExchange` event carrying bytes in the other
  direction. Exported: `api_symbols.csv:3307-3314`.
- `assets/protobuf/application.proto` at the pinned submodule commit:
  `StartRequest{name, args}`, `DataExchangeRequest{bytes data}`, `AppExitRequest`,
  `AppStateResponse`, `LockStatusRequest`.
- `applications/services/rpc/rpc_app.c:63-107`: `App.StartRequest` with
  `args == "RPC"` starts the named application with args `"RPC %08lX"` where the
  hexadecimal value is the address of the `RpcAppSystem`. The application parses it
  the way `applications/main/subghz/subghz.c:366-371` does with `sscanf`, sets its
  callback, and calls `rpc_system_app_send_started`. `StartRequest` on an
  application that is already running returns `ERROR_APP_SYSTEM_LOCKED`.
- `applications/services/rpc/rpc_cli.c` (read in full): the host opens an RPC
  session by sending the `start_rpc_session` CLI command on the CLI channel, after
  which the channel carries length prefixed protobuf. The session closes when the
  pipe returns short, which is what a cable pull produces, and the USB mode is
  locked for the life of the session.
- `applications/services/rpc/rpc.h:12` `RPC_BUFFER_SIZE (1024)`.

What this gives that candidate A does not: the host can launch the application
(`StartRequest` accepts a FAP path), can ask whether an application is running
(`LockStatusRequest` reports the loader lock, `loader_is_locked`), and the framing
is firmware owned. Every message is confirmed, so the host gets a typed timeout on
an unresponsive application.

What it costs: on session close the application's `RpcAppSystem` pointer is
invalid (`rpc_app.h`, `RpcAppEventTypeSessionClose`). There is no way to attach a
new session to a running application: `StartRequest` refuses because the
application is running, and `AppExitRequest` needs the session that just died. So
a cable pull means the application exits (or the host closes it over the CLI with
`loader close`) and is restarted on reconnection. The "Pi disconnected, reconnect
USB" display state in spec 2.6 cannot be shown by the application, because the
application is gone; the photographer sees the desktop. The appliance side must
speak Flipper's protobuf RPC, which is a dependency decision for the extension
document, not this one. The RPC session also locks the USB mode, so the CLI is
unavailable while it runs.

### Candidate C: GPIO serial

Two forms, both read from source:

- The expansion module protocol (`documentation/ExpansionModules.md`, read in
  full; exports at `api_symbols.csv:1125-1129`). It is designed for the Flipper
  as HOST of a module on the GPIO header, negotiates baud rate from 9600, requires
  the module to pull RX low to start and to send heartbeats within 250 ms, and
  tunnels the same RPC as candidate B. The appliance would be the module. This is
  candidate B with extra wiring and a 250 ms heartbeat obligation.
- A raw UART owned by the application: `furi_hal_serial_control_acquire`,
  `furi_hal_serial_init`, `furi_hal_serial_tx`, `furi_hal_serial_async_rx_start`
  are exported (`api_symbols.csv:1747-1771`), on pins 13 and 14 (USART) or 15 and
  16 (LPUART), 3.3 V logic.

Both need a second cable for power, or a GPIO power feed, and the expansion form
inherits candidate B's session loss on disconnection. Neither offers anything over
USB for a device that sits next to the appliance on a cable. Recorded so the
enumeration 4.2 asks for is complete, and not recommended.

### Recommendation for `FD2` (a recommendation, not a decision)

Candidate A, USB CDC in `usb_cdc_dual` mode with the application on channel 1 and
the firmware CLI left on channel 0.

Reasons, each traceable to a requirement:

- Spec 2.10 wants either side to restart without a repair step and wants the
  Flipper to show clearly when the appliance is unreachable. Candidate A keeps the
  application alive across a cable pull with both connect signals available;
  candidate B ends the application.
- Spec 2.8 and `FD2` guidance say not to invent a complicated protocol where a
  plain serial line suffices. Candidate A is a plain serial line. Candidate B puts
  protobuf under the protocol on both sides for a message set of six verbs.
- Keeping the CLI on channel 0 preserves the host's ability to launch the
  application with `loader open` and to read `free` and the log while the
  application runs, which is the observability spec 2.11 wants and the autostart
  substitute 4.1 needs.

Trade accepted: the host must find the right `ttyACM` node by serial string and
interface number, which is a udev rule on the appliance. Trade not yet measured:
what the Pi 5 running Debian 13 actually enumerates for a dual CDC device, and
whether the node is stable, which is HARDWARE for Part 6 of the extension.

`FD2` and `PD2` are one decision recorded in both documents. This recommendation
is made here because the peripheral is built first (`PD1`).

## 4.3 Power

Observed from source: `furi_hal_power_suppress_charge_enter` and
`furi_hal_power_suppress_charge_exit` are exported (`api_symbols.csv:1657-1658`)
and, at `targets/f7/furi_hal/furi_hal_power.c:485-496`, the first caller disables
charging at the battery charger (`bq25896_disable_charging`) with a reference
count so nested callers behave. `furi_hal_power_is_charging`,
`furi_hal_power_get_usb_voltage` and `furi_hal_power_get_battery_current` are also
exported (`:1634-1648`).

What this means: the application CAN stop the Flipper drawing charge current
from the appliance while connected, if the measured total draw does not fit the
budget. Whether it should is `FD10`. The USB input current limit the charger
negotiates was NOT read and is not claimed.

HARDWARE and unmeasured: total draw at the Pi with the guest radio adapter and the
Flipper both attached and active, with charging allowed and with it suppressed.
The main specification's 1200 mA figure is itself unverified. The Pi 5 also
behaves differently depending on the supply it detects, and that has not been
recorded in the main repository's evaluation either. This is owed to `FD10` and
to the PE2 hardware gate, and if the combination does not fit it is raised in the
main specification rather than solved here.

Partial hardware observation, 2026-09-11 (FE4 gate): with charging allowed, a
cable pull or replug repeatedly browned out the Pi enough to drop the author's
SSH session, and once appeared to crash it transiently. Entering charge
suppression on link open (`furi_hal_power_suppress_charge_enter`, matched by the
exit on close) removed the crash and reduced the SSH hang to an occasional brief
stall on a plug cycle, not gone. This is the FD10 mitigation applied ahead of its
measurement: a charging Flipper both loads the shared budget and makes an unplug
a larger current transient, and stopping the charge current addresses both. The
quantities behind it (total draw, the transient) remain unmeasured and owed to
FD10 and PE2; the observation is qualitative and from one session.

## Hardware findings, FE4 (2026-09-11)

Five defects surfaced only when the application ran on the device against the Pi.
Each sat in code the host tests do not cover, by the architecture's own division:
the SDK USB glue (`remote_transport.c`, `stopbath_remote.c`) and the peer's OS I/O
(`peer/development_peer_shell.c`) are the thin untested edges; the pure logic they
wrap was correct throughout. They are recorded here because the lesson is the
division itself, not the individual bugs.

1. Send from a stack buffer. `furi_hal_cdc_send` is asynchronous: it hands the
   buffer to the USB endpoint and returns before the bytes leave, so a buffer on
   the service call's stack was overwritten mid transmit and the peer saw
   malformed lines. Fixed by holding the transmit buffer on the transport and a
   `tx_complete` semaphore released by the tx complete callback, exactly as
   `usb_uart_bridge.c` does. This was the difference between no handshake and the
   first working one; the host tests could not see it because they move bytes
   through a synchronous in memory pipe.

2. USB mode locked during `ufbt launch`. Launch holds an RPC session, which locks
   the USB mode, so the application's `set_config(usb_cdc_dual)` failed and was
   never retried, leaving a permanent "Pi disconnected". Fixed with a one second
   retry of `remote_transport_open` in the main loop.

3. Stale DTR across a physical pull. A cable yank gives the host no chance to drop
   DTR, so the transport's `dtr_present` stayed true and a replug looked like a
   port already open, starting a handshake with nobody there. Fixed by clearing
   `dtr_present` in the state callback when USB is no longer connected.

4. Send wedge after a mid transmit pull. If the cable was pulled while a transmit
   was in flight, the tx complete interrupt never fired, so the semaphore stayed
   taken and every later send timed out for the life of the process. Verified
   three times. Fixed by normalising the semaphore to available on the port close
   edge in `remote_transport_service`.

5. Peer never detected the unplug. The most stubborn, and the last cleared. The
   peer opens the node `O_NONBLOCK` with `VMIN=0, VTIME=0`, in which `read`
   returns 0 for no data; a USB CDC unplug also makes `read` return 0, so the two
   are indistinguishable and the peer read zeros off the dead node forever,
   never re probing, never reopening the new node, never asserting DTR. The
   Flipper, correctly waiting for the host to open the port, stayed "Pi
   disconnected" with nothing wrong on its side. The tell was that the peer never
   printed "device gone" across repeated cycles while `ls /dev/ttyACM*` showed the
   Flipper re enumerating on a new node each time. Fixed by polling the node for
   `POLLHUP`/`POLLERR`, which the kernel raises distinctly on a tty hangup, before
   each pump. This is a development tool detail, not a device contract point: the
   real appliance identifies the channel by a udev rule on serial and interface
   (4.2) rather than by probing, and detects the detach through its own I/O layer.

Two lessons for the record. First, the node number is not stable across a
reattachment on this platform (ttyACM1 to ttyACM3 and back), and
`/dev/serial/by-id/` is absent on this Pi, so the peer must find the channel by
probing each `ttyACM` node for the HELLO only the application sends; a fixed node
assumption fails on the second connection. Second, every one of these five is in
the untested glue, which confirms the value of keeping that glue as thin as
possible: the state machine, protocol, and layout, all host tested, needed no
change at the gate.

## 4.4 Display and QR

Observed from source:

- Display is 128 by 64 pixels, monochrome (`lib/u8g2/u8g2_glue.c:126-131`
  `pixel_width = 128, pixel_height = 64`; `applications/services/gui/canvas.c:28`
  sets up that driver).
- Drawing primitives exported (`api_symbols.csv:865-893, 1073`): `canvas_draw_box`,
  `canvas_draw_dot`, `canvas_draw_frame`, `canvas_draw_str`,
  `canvas_draw_str_aligned`, `canvas_draw_xbm`, `canvas_set_font`,
  `canvas_string_width`, `canvas_invert_color`, `elements_multiline_text_aligned`.
  A QR matrix can be drawn with `canvas_draw_box` per module, or the whole matrix
  rendered once into an XBM bitmap and drawn with `canvas_draw_xbm`.
- Fonts (`applications/services/gui/canvas.h:26-30`): `FontPrimary`,
  `FontSecondary`, `FontKeyboard`, `FontBigNumbers`, `FontBatteryPercent`.
  Mapped in `canvas.c:165-173` to `u8g2_font_helvB08_tr`,
  `u8g2_font_haxrcorp4089_tr`, `u8g2_font_profont11_mr`,
  `u8g2_font_profont22_tn` and `u8g2_font_5x7_tr`; parameters at
  `canvas.c:9-14` (Primary height 8, leading 12; Secondary height 7, leading
  11; BigNumbers height 15, leading 18).
- Glyph advances, DECODED 2026-09-11 from `lib/u8g2/u8g2_fonts.c` at the
  pinned commit by `scripts/generate_font_metrics.py` (u8g2 font header as
  read by `u8g2_font_setup`, then each glyph's delta x). Primary: printable
  average 5.86 px, lowercase 5.62, uppercase 7.35, widest 11 (`@`, `W`).
  Secondary: average 5.21, lowercase 4.73, uppercase 6.00, widest 9 (`@`).
  Measured widths of every label the FD3 layouts place, all of which fit
  their area: the tightest is `999+ delivered` at 64 px in the 66 px column
  (Secondary). Centred Primary lines are at most 85 px (`Recovery needed`)
  in 124 px.
- Consequence for `FD8`, recorded here rather than decided: the error band
  is 124 px in Secondary, and upper case with underscores runs about 6 px a
  character, so an error code longer than about 20 characters is truncated
  on this display. `TERMINATION_NOT_PERMITTED` from the seam document's
  proposal measures 142 px. Beside a code the band is narrower still, 66 px,
  about 11 upper case characters, because it cannot cross the code. Either
  the codes stay short (11 characters fit everywhere, 20 fit off the code
  page) or the device shows their start; the protocol draft in FE3 should
  choose knowingly.
- The GUI service thread has a 2 KB stack (`applications/services/gui/application.fam:11`),
  which is what a view port's draw callback runs on. The application composes
  its screen on its own thread and only replays it from the draw callback.
- Backlight: `sequence_display_backlight_enforce_on`,
  `sequence_display_backlight_enforce_auto`, `sequence_display_backlight_off` are
  exported notification sequences (`api_symbols.csv:5385-5388`). The application
  can keep the backlight on while presenting a QR and hand control back on exit.
  Whether the backlight helps or hurts scanning in daylight is HARDWARE.

QR arithmetic, restated from spec 2.7 against the measured geometry: a version 1
symbol is 21 modules; with the quiet zone it is 29; at 2 pixels per module that is
58 pixels, which fits the 64 pixel height with 6 pixels spare and leaves 70 pixels
of width for text. At 3 pixels per module a version 1 symbol is 87 pixels and does
not fit. A version 2 symbol (25 modules, 33 with quiet zone) at 2 pixels per module
is 66 pixels and does not fit the height either. So on this display the only
whole-pixel choice is version 1 at 2 pixels per module, unless the quiet zone is
reduced below the standard four modules, which is a scanning risk to test rather
than assume. The `http://192.168.72.1/` fallback address recorded in the main
repository is 20 characters; version 1 at error correction level L holds 25
alphanumeric characters or 17 bytes, and whether that URL fits depends on the
encoder's mode selection (the alphanumeric mode has no lowercase letters, so a
lowercase URL is byte mode, and 20 bytes exceeds 17). This is a concrete reason
`FD18` matters: uppercase `HTTP://192.168.72.1/` may fit where the lowercase form
does not, and the same address as a hostname does not fit at all. None of this
replaces scanning with real phones, which is HARDWARE and `FD11`.

HARDWARE and unmeasured: rendering time and memory for the QR on the device;
daylight contrast and readability at arm's length; scanning reliability across a
representative set of phones (`FD11`), including the Google camera application
join path failure recorded in the main repository's 4.6, which applies to any
Wi-Fi QR regardless of what displays it.

### QR encoder, dependency record (spec 0.12), ADDED 2026-09-11

Added for the author's FD4 experiment (a real Wi-Fi payload rendered on the
device), which brings the encoder forward from FE5. The FE5 criteria that are
not yet met are listed at the end and are not claimed.

| Item | Observed |
|---|---|
| Dependency | `https://github.com/nayuki/QR-Code-generator`, the C implementation |
| Pinned at | tag `v1.8.0`, commit `720f62bddb7226106071d4728c292cb1df519ceb`, 2022-04-17, confirmed with `git ls-remote --tags` before a shallow clone |
| Files vendored | `c/qrcodegen.c` (1022 lines) and `c/qrcodegen.h` (385 lines), unmodified, under `lib/qrcodegen/` with `PROVENANCE.md` carrying their sha256 digests |
| Behaviour provided | QR Model 2 encoding of text into a module matrix: mode selection, Reed-Solomon error correction, mask selection, version selection within a caller supplied range |
| Why the SDK is insufficient | the firmware at the pinned commit contains no QR encoder (`grep -rli qrcodegen` and `qrcode` over the tree returned nothing), and the error correction and masking are not something to write by hand (spec 0.12 guidance) |
| Maintenance status | last upstream push 2026-08-31, not archived (GitHub API, 2026-09-11); the C implementation has not changed since `v1.8.0` in a way this project depends on, and the pin is by commit |
| Licence | MIT, in the header of both files and in the upstream `Readme.markdown` at that commit, copied to `lib/qrcodegen/LICENSE.txt`. Same licence as this repository and StopBath |
| Size on device | the FAP grew from 12,080 to 20,164 bytes with the encoder, the wrapper, the bitmap and the fixtures; `.text` from 7,027 to 11,904 bytes |
| Allocation | none on the heap, stated in the source header (`qrcodegen.c:45-46`) and confirmed by grep: no `malloc`, `calloc`, `free` or `alloca`. Buffers are caller supplied and sized by `qrcodegen_BUFFER_LEN_FOR_VERSION`, 106 bytes for the version 3 ceiling |
| Security implications | it consumes the payload the appliance supplies, which is untrusted input on the same terms as the rest of the protocol. It writes only into the two caller sized buffers, both bounded at compile time in `remote_display/remote_qr.h`. `qrcodegen_encodeText` refuses rather than overruns when the text does not fit the version range. It runs under the sanitiser build on every test run |
| Test strategy | `tests/test_remote_qr.c`: version selection for the two experiment payloads, finder patterns at the three corners, the 53 byte bound of version 3 at the lowest error correction exactly at and one over, empty payload refused, bitmap placement. Matching full matrices against published vectors is FE5's criterion and is NOT yet done |
| Replacement cost | the wrapper in `remote_display/remote_qr.c` is the only caller, four functions, and holds the version ceiling; another encoder would replace one file and the private library entry |
| Warnings | compiles clean under the SDK's set on the device and on MinGW gcc 15. Linux gcc 15 on aarch64 flags `-Wconversion` at four lines inside the library; that flag is this project's extra above the SDK set, so the host build applies the SDK set to vendored code and the extras to project code (`Makefile`) |

Display ceiling, derived and now enforced in `remote_display/remote_qr.h`: the
58 pixel code area at two pixels per module holds 29 modules, which is version
3. Version 4 (33 modules) does not fit at two pixels. So version 3 is the
ceiling this display imposes, and the wrapper refuses anything larger rather
than drawing something unscannable. Capacities that matter, from the library's
own tables at the lowest error correction, byte mode: version 1 holds 17,
version 2 holds 32, version 3 holds 53. Error correction starts at the lowest
level and the library raises it when the same version has room.

Consequences for `FD4`, arithmetic only, scanning is HARDWARE:

- The author's experiment payload, a fifteen character SSID and an eight
  character passphrase in StopBath's grammar (kept out of the repository in
  `experiment_credentials.h`, see the example header), is 41 bytes: version
  3, filling the code area with no quiet zone inside it.
  The quiet zone is whatever surrounds the area: three pixels above, three
  below, two to the right, and the screen edge to the left.
- A real session payload in StopBath's grammar, `StopBath-2AWZS7` and a twenty
  character passphrase, is 53 bytes: exactly version 3's ceiling at the lowest
  error correction. One more character of SSID or passphrase and the Wi-Fi
  page cannot be shown at this size. The memorable passphrase style
  (`adjective.noun.NNN`, up to about 18 characters) fits with a little room.
- The gallery address `HTTP://192.168.72.1/`, upper case for alphanumeric
  mode, is version 1 at 42 pixels, centred with a four module quiet zone.

## NFC: `FD15`, `FD16`, `FD17`

Observed from source, with the caveat that none of it has been run on the device:

- A Type 4 Tag listener exists and is registered
  (`lib/nfc/protocols/nfc_listener_defs.c:26`, protocol `NfcProtocolType4Tag` at
  `lib/nfc/protocols/nfc_protocol.h:192`). Its data model
  (`lib/nfc/protocols/type_4_tag/type_4_tag.h`) carries the NDEF file as a
  `SimpleArray* ndef_data` plus `ndef_max_len`, `ndef_file_id`, and the read and
  write lock bytes, and the listener serves reads from that array
  (`type_4_tag_listener_i.c:135-163`).
- The pieces an application needs are exported: `nfc_alloc`, `nfc_listener_alloc`,
  `nfc_listener_start`, `nfc_listener_stop`, `nfc_listener_free`
  (`api_symbols.csv:3000-3036`), `type_4_tag_alloc` and friends (`:3876-3887`),
  and `simple_array_alloc`, `simple_array_init`, `simple_array_get_data` with
  `simple_array_config_uint8_t` (`:3360-3368, 5414`).
- `nfc_device_type_4_tag` is marked `-` (`:5361`), so the application cannot use the
  generic device save and load path for this protocol, which it does not need.

So `FD15` (runtime NDEF payload) has an API surface: the NDEF file bytes are built
in memory and handed to the listener. Whether a phone reads it is HARDWARE. `FD16`
(QR on screen while NFC is active) is plausible from the structure, since the
listener runs on its own worker and the application owns the canvas, but it is
HARDWARE. `FD17` (a Wi-Fi credential record Android accepts) is entirely
HARDWARE, and no in-tree Wi-Fi record construction exists to lean on: the only
NDEF code in `applications/` is the reader-side parser plugin
`applications/main/nfc/plugins/supported_cards/ndef.c`.

### NDEF record construction, READ 2026-09-11 for the FD15 to FD17 experiment

The record grammar was taken from two implementations that read it, not from
the NFC Forum or Wi-Fi Alliance specifications, which were not consulted:

- The firmware's own NDEF parser,
  `applications/main/nfc/plugins/supported_cards/ndef.c` at the pinned
  commit: the record header flags byte (message begin 0x80, message end
  0x40, chunk 0x20, short record 0x10, identifier length present 0x08, type
  name format in the low three bits, lines 76 to 87), the header walk (type
  length, one or four byte payload length, optional identifier length, type,
  lines 684 to 730), the URI prefix table (lines 92 to 110: 0x03 is
  `http://`, 0x04 is `https://`, 0x00 is no prefix), and the Wi-Fi record
  (lines 482 to 560: media type `application/vnd.wfa.wsc`, credential
  attribute 0x100E containing SSID 0x1045, network key 0x1027 and
  authentication type 0x1003 with WPA2 personal 0x0020; attributes are big
  endian id and length).
- Android's `NfcWifiProtectedSetup.java`, which the firmware parser cites and
  which is the thing that has to accept the record. Read at the commit the
  firmware cites (`025560080737b43876c9d81feff3151f497947e8` in
  `platform/packages/apps/Nfc`) and again at the current main of the mainline
  module (`platform/packages/modules/Nfc` commit
  `b01e3991865799e55d095a30a0b86b5f4030b929`,
  `NfcNci/src/com/android/nfc/NfcWifiProtectedSetup.java`), identical in
  what matters: it finds the first record of that media type, walks
  attributes to the credential, and inside it reads SSID, network key (at
  most 64 bytes, otherwise null) and authentication type (exactly two bytes,
  otherwise null); unknown attributes are skipped; a configuration is
  returned when the SSID is present and a key is present for a non open
  type. No MAC address, network index or encryption type attribute is read.

So the minimal record Android accepts is a credential with SSID,
authentication type and network key, which is what
`remote_display/remote_ndef.c` builds. The record is derived from the same
Wi-Fi code payload the QR is rendered from, by parsing the grammar StopBath
emits with its backslash escapes (specification 2.7: one value per page).

### Type 4 Tag emulation, READ 2026-09-11

`lib/nfc/protocols/type_4_tag/type_4_tag_listener_i.c` at the pinned commit
implements the ISO7816 file system a reader expects: SELECT by name for the
NDEF application, SELECT by identifier for the capability container (0xE103)
and the NDEF file (0xE104 unless the data is tag specific), READ BINARY of a
capability container synthesised by `type_4_tag_cc_dump` from the data
(defaults when `is_tag_specific` is false: NDEF file 0xE104, 2046 bytes
maximum, no locks), and READ BINARY of the NDEF file with the two byte
length prepended by the listener itself. `Type4TagData.ndef_data` is
therefore the bare NDEF message. `type_4_tag_alloc` resets the data, so a
fresh allocation plus the message plus an identity is the whole model.

Identity: `iso14443_4a_reset` (`iso14443_4a.c:56-68`) sets a minimal ATS
(TL 1, nothing else) which `iso14443_4a_listener_send_ats` transmits as a
single byte. The ISO14443-3A layer sends `uid`, `atqa` and `sak` from the
data (`iso14443_3a_listener.c:44-47`). The firmware decides a card speaks
ISO14443-4 by `sak & ISO14443A_ATS_BIT` where the bit is `1 << 5`
(`iso14443_3a.c:6, 162-166`), so the experiment sets SAK 0x20. ATQA is
{0x44, 0x00}, the value the firmware's unit test uses for a seven byte UID
card (`applications/debug/unit_tests/tests/nfc/nfc_test.c:215-219`). The UID
is arbitrary.

MEASURED 2026-09-11 by the author, first attempt, with the minimal one byte
ATS: an Android phone held to the device for a whole carousel cycle read
nothing on either page. The phone's NFC was confirmed working in the other
direction (the Flipper read the phone's payment emulation), which says
nothing about the phone as reader.

Cause identified from source, not yet confirmed on hardware: with no TB1
byte, `iso14443_4a_get_fwt_fc_max` (`iso14443_4a.c:240-254`) returns
`ISO14443_4A_FDT_DEFAULT_FC`, defined at `iso14443_4a.c:14` as
`ISO14443_3A_FDT_POLL_FC`, 1620 carrier cycles, about 120 microseconds. A
reader applying the same defaults expects an answer to every block within
that, and this application answers from a thread. The second build spells
the ATS out from the bit definitions in `iso14443_4a_i.h:9-23` and the
decoders in `iso14443_4a.c:222-313`: TL 5, T0 0x78 (TA1, TB1, TC1 present,
FSCI 8 = 256 byte frames, matching the 3A layer's buffer), TA1 0x80 (106 kbit
both ways, compulsory), TB1 0x80 (FWI 8, about 77 ms; SFGI 0), TC1 0x02 (CID
supported). FWI 8 is a choice with margin, not a measurement.

CONFIRMED on hardware 2026-09-11 by the author: with that ATS the phone read
the Wi-Fi credential record and the cause above stands as the explanation of
attempt one. Recorded for the appliance side too: the identity a peripheral
presents over NFC is the peripheral's business and never crosses the link.

The firmware's own emulation path is the reference for the calls:
`nfc_scene_emulate_on_enter_type_4_tag` in
`applications/main/nfc/helpers/protocol_support/type_4_tag/type_4_tag.c:165-170`
does `nfc_listener_alloc(nfc, NfcProtocolType4Tag, data)` then
`nfc_listener_start(listener, callback, context)`. The Type 4 Tag listener's
only event is `Type4TagListenerEventTypeCustomCommand`
(`type_4_tag_listener.h`), raised for commands it does not handle, so a
successful read produces no event at all; the phone's behaviour is the only
signal.

Experiment status, HARDWARE, all three still open:

- `FD15` runtime NDEF: the experiment build presents the page's record while
  the page shows. Whether a phone reads it is the answer.
- `FD16` simultaneous QR and NFC: the code stays drawn while the listener
  runs. Whether the display and the NFC worker coexist is the answer.
- `FD17` Wi-Fi credential Android accepts: the record is what Android's
  parser reads. Whether the join prompt appears is the answer.

## Lock: `FD19`

Answered from source. The firmware's own lock is NOT usable by this application.

- `desktop_api_is_locked` and `desktop_api_unlock` are marked `-` in
  `api_symbols.csv:984, 986`. `desktop_lock` is private
  (`applications/services/desktop/desktop.c:403`). Neither can be called from a FAP.
- The desktop's automatic lock is inhibited while any application is running
  (`desktop.c:147-155`: `DesktopGlobalAutoLock` locks only when `!app_running`).
- When the desktop IS locked, the GUI enters lockdown (`desktop.c:427`
  `gui_set_lockdown`), and in lockdown the GUI draws and routes input to the
  desktop layer only (`applications/services/gui/gui.c:252-262, 302-306`). An
  application's viewport is hidden and receives nothing. `gui_set_lockdown` is
  exported (`api_symbols.csv:2094`) but calling it would hide this application's
  own screen, which is the opposite of what 2.4 wants.

Conclusion for the author: an application level lock is required. It is
presentation state, owned by the application, entered by a short down press and
left by a long down press, made obvious on the display, and reported to the
appliance as status, exactly as 2.4 already describes. The guidance to prefer the
firmware's own lock cannot be followed at this API version, and this document
records why.

## Decisions still needing the author

| Decision | State after this evaluation |
|---|---|
| `FD1` Language and toolchain | DECIDED by the author 2026-09-11: C, built against the release SDK zip `flipper-z-f7-sdk-unlshd-086.zip` with `ufbt`. Toolchain is `gcc-arm-none-eabi 12.3`, package 39. CONFIRMED at FE1 the same day: `ufbt 0.2.6` accepts the zip and the application builds, see 4.1. |
| Enum representation | RECORDED at FE3, 2026-09-11: the device toolchain builds with `-fshort-enums` (an enum takes the smallest integer that fits), so a small enum is unsigned there and a `value < 0` bounds check is provably false and rejected by `-Werror=type-limits`, while host gcc 15 without the flag accepts it. The generated tables use an unsigned range check instead, which is correct on both. The lesson: a warning clean host build does not guarantee a warning clean device build, so the FAP build (V2) is not optional cover. |
| `FD2` Transport | DECIDED by the author 2026-09-11: candidate A, USB CDC in `usb_cdc_dual` mode, application on channel 1, firmware CLI kept on channel 0. `PD2` in the extension document must record the same. |
| `FD3` Display layouts | DECIDED by the author 2026-09-11: the six screens proposed against the measured geometry (header band 0 to 11 with the lock shown there; READY with a centre button hint; QR page with the code at 0,3 size 58 and a 66 pixel text column from x 62; TERMINATING, RECOVERY_REQUIRED and NOT CONNECTED as two centred lines; a 12 pixel inverted error band at y 52). AMENDED the same day on the author's observation from the device: the header as proposed sat inside the code area. Beside a code, the header, the lock band and the error band are all confined to the column (x 60 to 127); when locked there the product name gives way to LOCKED, since both do not fit 66 pixels. On screens with no code they stay full width. A test now asserts nothing is placed over the code area. Font metrics from `applications/services/gui/canvas.c:9-14` at the pinned commit: Primary `helvB08` height 8, Secondary `haxrcorp4089` height 7, BigNumbers `profont22` height 15. Implemented as the pure layout in `remote_display/`. |
| `FD4` Wi-Fi QR viability | Arithmetic revised 2026-09-11 with the encoder in hand: a Wi-Fi payload fits version 3, which is the display ceiling, with no quiet zone inside the code area, and a real session payload sits exactly at that ceiling (53 bytes). The author's experiment build renders one for scanning. MEASURED 2026-09-11 by the author: the version 3 code scanned, from about 5 cm. Handset and app pending in `HARDWARE_COMPATIBILITY.md`. A scan that needs 5 cm is a finding for `FD11` and Part 8 (the guest's phone has to come that close), not a failure. |
| `FD10` Power | Charge suppression is available. Measurement outstanding. |
| `FD11` Phones | Not started. |
| `FD12` Firmware distribution | ANSWERED by the author 2026-09-11: Unleashed, `unlshd-086`. The variant question is CLOSED as irrelevant to the API, see 4.1. |
| `FD13` Name and licence | DECIDED by the author 2026-09-11: `stopbath-flipper`, MIT, `Copyright (c) 2026 TheScottBot`, the same text as the StopBath `LICENSE`. |
| FE2 application stack | 4096 bytes in `application.fam`, unmeasured; the composed layout is 696 bytes and the display state 308, both held statically, with one further layout on the stack during composition. Measured with the CLI `top` command at the FE2 gate. |
| Long press duration (Appendix A) | DECIDED by the author 2026-09-11 for FE1: the firmware's own `InputTypeLong` classification, 300 ms at the pinned commit, isolated behind one function in `stopbath_remote.c`. Revisited with the device in hand at FE4, when `CENTER_LONG` first means something. |
| Back long press exits | CONFIRMED by the author 2026-09-11. Recorded in `IMPLEMENTATION_DEVIATIONS.md`. |
| `FD15`, `FD16`, `FD17` NFC | ANSWERED on hardware 2026-09-11 by the author, second attempt (see the NFC section): `FD15` yes, an application can present an NDEF target whose payload is chosen at runtime, through the Type 4 Tag listener; `FD16` yes, the code stayed drawn while the listener ran and the phone read it; `FD17` yes, Android accepted the Wi-Fi credential record built as its parser reads it and the join succeeded; the gallery page's URI record was read and the address opened. Handset: Samsung Galaxy Z Fold3 (the StopBath repository's `IMPLEMENTATION_DEVIATIONS.md` records the author's Fold as `SM-F926B` on Android 15, measured 2026-09-04). iPhone not yet tried. The FE6 phase is therefore not dropped. |
| `FD18` Guest gallery address | Blocked on the `D30` reference and the `guest.stopbath.photo` conflict in the main repository, both raised to the author 2026-09-11. |
| `FD19` Screen lock | ANSWERED from source: application level lock. |
| `PD11` Protocol definition format | No longer blocks anything here. Under the revised build order (extension 1.4, Flipper 2.9) it is taken at promotion, after `FD20`. The seam document's 4.8 proposal is the input to the FE3 draft. |

## Defects raised against other documents (2026-09-11)

Recorded here so they are not lost; none is decided here.

1. `STOPBATH_PERIPHERAL_EXTENSION.md` 3.3 cites `D30` in the main specification.
   `SPEC.md` decisions end at `D27`.
2. `SPEC.md` `D3` forbids a publicly registered name as the guest local hostname;
   the main repository's evaluation 4.6 records `guest.stopbath.photo` as settled
   and in use.
3. `STOPBATH_PERIPHERAL_EXTENSION.md` 2.2 removes the continuous connection
   interval and the same-batch-as-handshake rejection, while 4.2, 5.1 and PE3 still
   carry them.
4. `SPEC.md` 0.8 was amended by the author to a local em dash scan rather than a
   continuous integration step; `STOPBATH_FLIPPER_SPEC.md` 0.8 still mandates the
   continuous integration step. Whether the amendment carries over is the author's.
5. This repository's `README.md` at the initial commit was UTF-16 encoded with a
   byte order mark. It was removed by the author in the commit "Deleted readme"
   and recreated as UTF-8 at FE1 on 2026-09-11.
6. The revised Flipper specification's `FE1` carries two "Tests first"
   paragraphs; the second is `FE3`'s parser list, duplicated. FE1 was worked to
   the first. The seam document records this as its defect 17.
7. The revised Flipper specification's `FE5` and Part 7 item 5 still have the
   Flipper switching between two held payloads, which its own 2.5 now forbids.
   The seam document records this as its defect 4.
8. The revised `FE1` assumption "that the firmware exposes button events and lock
   state the way 4.1 assumed" names a firmware lock state that `FD19` found does
   not exist for an application. Button events were confirmed; the lock is the
   application's own and is covered by its tests.

Defects 1 and 2 above are RESOLVED in the main specification as of the author's
2026-09-11 revision: `D30` now exists and settles the gallery address on the bare
local address, and `D3` was amended alongside it.
