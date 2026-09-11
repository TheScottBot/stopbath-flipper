# Protocol

## Where the definition lives, and when

The peripheral protocol is owned by StopBath and, once frozen, lives in the
StopBath repository (specification 2.8, extension 4.1). Until the freeze it is
drafted here, because the peripheral is built first (specification 2.9,
extension 1.4, `PD1`).

The sequence:

1. `FE1` and `FE2` have no protocol. This is that period. Nothing in this
   repository parses, encodes, or transmits anything.
2. `FE3` drafts the protocol in this file, builds the parser and encoder as a
   freestanding C library, and builds the development peer. The appliance's
   stated requirements on the protocol are Part 4 of `docs/PERIPHERAL_SEAM.md`
   in the StopBath repository, and the draft is made against them.
3. From `FE3` until `FD20` the protocol is provisional and freely revisable.
   Every change is recorded below with its reason.
4. At `FD20` the author accepts it, it is promoted to the StopBath repository
   under review, field by field, and from then on this file references the
   promoted definition rather than carrying it. Rule 0.11 applies in full from
   that point.

## Change record

Required by specification 2.9 for every change made before the freeze. Empty
until `FE3` produces a first draft.

| Date | Change | Reason |
|---|---|---|

## What is already fixed

The control table in specification 2.3 is the fixed shape of the product and
is not provisional. The events the remote reports are, and will remain,
`CENTER_SHORT`, `CENTER_LONG`, `LEFT_SHORT`, `RIGHT_SHORT`, and `BACK_SHORT`.
Their wire spelling is decided in `FE3`; their existence and meaning are not.

The down button is handled on the device as the screen lock and is reported
as status, never as a command.
