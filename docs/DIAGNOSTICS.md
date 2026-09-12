# Diagnostics: watching the Flipper app live

The application logs what it is doing to the firmware's own log, and leaves the
firmware command line running the whole time, so you can watch that log from the
appliance (or any development machine) while the link is up. This is how to see,
from the Pi, whether a link problem is the Flipper's doing or the appliance's.

## Why there are two serial nodes, and which is which

The app runs USB in dual CDC mode: it takes **channel 1** for the peripheral
link and leaves the firmware command line on **channel 0**. So a connected
Flipper shows up as *two* `/dev/ttyACM*` nodes:

- one carries the **peripheral protocol** (the appliance opens this one; if you
  open it yourself you will see a line like
  `HELLO version=1 peripheral=flipper-zero locked=0`);
- the other is the **firmware command line** (opening it and pressing Enter
  gives a Flipper prompt). This is the one to read the log on.

The node numbers are not stable across a reattach (they shift `ttyACM1` →
`ttyACM3` and back), so identify by behaviour, not by number. When the appliance
already holds the protocol node, the *other* `ttyACM` is the command line.

## Reading the log from the Pi (no extra tools)

```bash
# Pick the command-line node (the ttyACM the appliance is NOT using).
stty -F /dev/ttyACM0 raw -echo 115200
printf 'log\r\n' > /dev/ttyACM0
cat /dev/ttyACM0
```

`cat` streams the log until Ctrl-C. The baud rate is ignored by USB CDC but a
value is required. If the node is the wrong one you will see protocol lines or
nothing useful; try the other `ttyACM`.

To capture to a file while you reproduce a fault:

```bash
printf 'log\r\n' > /dev/ttyACM0
cat /dev/ttyACM0 | ts '[%H:%M:%.S]' > /tmp/flipper.log   # ts is optional, from moreutils
```

Correlate it with the appliance's own log by time:

```bash
journalctl -u stopbath -f -o cat | grep -i peripheral
```

## What the trace prints

Two tags. `StopBathLink` is the physical USB edge; `StopBathRemote` is the
application and session. Every line is emitted only on a change, so the log is
quiet when nothing is happening.

| Line | Means |
|---|---|
| `StopBathLink port opened (usb=1 dtr=1): handshaking` | the cable is in and the appliance opened the port; the app is about to send HELLO |
| `StopBathLink port closed (usb=0 dtr=0): unsent dropped` | the cable was pulled or the appliance closed the port; any queued press is dropped |
| `StopBathRemote link X -> Y` | the link state moved between `link-down`, `handshaking`, `connected`, `incompatible` |
| `StopBathRemote handshake retry #N: no DISPLAY acceptance within 2000ms, resending HELLO` | the app sent HELLO and never got the DISPLAY that accepts it, so it is trying again |
| `StopBathRemote DISPLAY status=S error=CODE` | a record arrived from the appliance; `error=` shows a rejection such as `ACTIVE` or `BAD_VALUE` (the same code the screen shows) |
| `StopBathRemote reconnections=N` | the link has dropped and re-handshaked N times |
| `StopBathRemote malformed or unexpected from appliance total=N` | the appliance sent something that is not a valid DISPLAY |
| `StopBathRemote button dropped by guard total=N` | a press was not sent because the screen was locked or the app was not foregrounded |
| `StopBathRemote button dropped, outbound queue full total=N` | presses arrived faster than the link drained them |

## Reading the common faults

- **Stuck on "reconnecting" after a replug while a session is live.** The trace
  shows `port opened -> handshaking` and then repeated `handshake retry #…`
  lines with no `DISPLAY status=…` in between. That means the app is sending
  HELLO but the appliance is not sending the DISPLAY that accepts it. The fix is
  appliance side (send the current DISPLAY after every successful HELLO). The
  retry lines are the app trying to recover on its own.
- **A button shows an error code.** Look for `DISPLAY status=… error=CODE`. The
  code is the appliance's rejection of the press; the Flipper only displays it.
  `error=ACTIVE` is a redundant start while a session runs; `error=BAD_VALUE` is
  the appliance rejecting an event value it should accept.
- **The device freezes after button spam.** Watch for a burst of
  `DISPLAY … error=…`, then `port closed` / `link connected -> link-down`
  (the appliance dropped the link), then a stuck handshake as above.
