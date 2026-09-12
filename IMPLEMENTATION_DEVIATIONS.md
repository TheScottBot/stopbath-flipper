# Implementation deviations

Every unavoidable deviation from the specification is recorded here with a
one line justification and a covering test (specification 0.11). Each entry
states the contract, what was done instead, why, and the test that holds it.

## A long back press exits the application

- The contract: specification 2.3, "**MUST NOT** extend local handling beyond
  the screen lock. Every other button reports and nothing more." The control
  table lists back short press as reported and does not list back long press
  at all.
- What was done instead: a long back press while unlocked ends the
  application. It is handled locally and never reported. While locked it is
  suppressed like every other press, so a pocket cannot leave the application
  and silently drop the link.
- Why: an application with no exit cannot clear the `FE1` hardware gate, since
  the author has to be able to leave it without rebooting the device. Back
  short is taken by the contract as a reported event, so the exit has to be a
  gesture the table does not use, and a long press of the same button is the
  one every other Flipper application already means by "leave".
- What this is not: a recommendation that the appliance ever sees it. The
  exit is presentation lifecycle, which specification 2.2 gives to the
  Flipper, and no verb will carry it.
- Author decision: CONFIRMED 2026-09-11. The exit stands.
- Update 2026-09-12: `BACK_SHORT` was dropped from the protocol at promotion
  (seam Q13). A short back press now reports nothing, so the exit gesture no
  longer shares a button with a reported event; the long back press is the only
  thing the back button does, and the tension this deviation was written around
  is gone. The deviation stands only as the record of why the exit is local.
- Covering tests: `a_long_back_press_while_unlocked_requests_exit` and
  `a_long_back_press_while_locked_is_suppressed` in
  `tests/test_remote_input_model.c`.

## The QR and NFC surfaces were built and proven before FE3, ahead of their phases

- The contract: the phase order in Stage two of the specification, which
  places the QR encoder in `FE5` and NFC presentation in `FE6`, both after the
  protocol (`FE3`) and the transport (`FE4`), and 0.4, which asks that a phase
  be worked in order with its tests first.
- What was done instead: on 2026-09-11, with `FE1` and `FE2` cleared, the
  author directed two experiments before `FE3`: render a real Wi-Fi code and
  a real gallery address on the device, then present the same two values
  over NFC. The QR encoder (`lib/qrcodegen`, `remote_display/remote_qr.c`)
  and the NDEF builder (`remote_display/remote_ndef.c`) were built for them,
  each tests first with its own suite, and both surfaces were confirmed
  working on hardware the same day: the version 3 Wi-Fi code scanned, an
  Android phone joined the network from the Wi-Fi credential record, and
  opened the gallery address from the URI record.
- Why: `FD4` and `FD15` to `FD17` were the largest unknowns in the whole
  plan, and every one of them is a hardware answer. Proving the guest facing
  surfaces first meant the protocol is now drafted knowing what the device
  can actually show and present, rather than finding out after the appliance
  side had been designed around it. That is the same reasoning the revised
  build order (`PD1`, Flipper 2.9) applies to the protocol, applied one step
  further.
- What it does not change: `FE5` and `FE6` still exist and still owe what
  the experiments did not do. `FE5` owes published matrix vectors and the
  distinct on screen error for a refused payload. `FE6` owes vectors from a
  published NDEF source, the iPhone confirmation, and the
  clearing of the presented record on session end once there is a session.
  The experiment credentials mechanism (`experiment_credentials.h`) is for
  the experiments only and is removed when payloads arrive over the link.
- Author decision: directed by the author, 2026-09-11, and confirmed on
  completion ("everything is verified").
- Covering tests: `tests/test_remote_qr.c` and `tests/test_remote_ndef.c`.

## The foreground guard always reports foregrounded on this hardware

- The contract: specification 2.4, which requires that no button event be
  transmitted while the application is backgrounded, the display is off, or
  another application is running, and 2.2, which has the event carry a
  foregrounded flag the appliance enforces.
- What was done instead: the application always reports `foregrounded=1`, and
  does not separately detect a display-off state.
- Why: a Flipper FAP has no background state to detect. The loader runs
  exactly one application at a time, so "another application is running"
  cannot occur; and when the desktop locks, the GUI enters lockdown and
  routes input to the desktop layer only, so this application's viewport
  stops receiving input entirely (the FD19 finding, evaluation log Lock
  section). Any input the application receives is therefore received while
  foregrounded, and the flag is honestly true. Display-off is not separately
  detected because the operative guard for the pocket case is the screen
  lock (2.4: "A long press plus the lock is the design"), and the workflow is
  to lock before pocketing; a press while the backlight has merely timed out,
  with the screen unlocked, is the photographer still using the device.
- What this does not weaken: the appliance still enforces the guard on the
  flags it receives (2.2), so a different or faulty peripheral sending
  `foregrounded=0` is rejected. The lock half of the guard is enforced in
  full, in `remote_input_model` and again in `remote_session`.
- Revisit when: a future firmware gives a FAP a real background or focus
  state, at which point `application_is_foregrounded` in `stopbath_remote.c`
  is the one place to change.
- Author decision requested: whether this stands. Recorded 2026-09-11.
- Covering tests: the guard itself is covered in `tests/test_remote_session.c`
  (`a_button_is_transmitted_only_while_connected_and_foregrounded`); the
  foreground determination is not testable off the device.

## The hyphen run rule is not applied to vendored third party sources

- The contract: specification 0.8, which requires the em dash scan and adds
  that converter mangled double and triple hyphens must be scanned for in
  prose as well.
- What was done instead: `scripts/check_typography.py` applies the em dash and
  en dash rule to everything, but skips the hyphen run rule under `lib/`, the
  folder the build system reserves for vendored private libraries. The one
  library there, `qrcodegen`, has comment banners made of runs of four
  hyphens and C decrement operators that the rule would flag: 33 hits, none
  of them prose.
- Why: the vendored files are upstream's verbatim, pinned by commit and by
  sha256 digest in `lib/qrcodegen/PROVENANCE.md`. Editing them to satisfy a
  rule written for this project's own prose would break the provenance, and
  the rule's purpose (a document converter's damage) does not arise in them.
  The em dash rule still applies in full and the files pass it.
- Author decision: taken by this implementation on 2026-09-11 alongside the
  encoder, for the author to confirm.
- Covering test: the scan itself, `make check-typography`, which passes with
  the library present and fails on an em dash placed under `lib/`.
