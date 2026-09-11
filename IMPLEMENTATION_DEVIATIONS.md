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
- Covering tests: `a_long_back_press_while_unlocked_requests_exit` and
  `a_long_back_press_while_locked_is_suppressed` in
  `tests/test_remote_input_model.c`.
