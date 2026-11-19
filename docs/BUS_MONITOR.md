# Bus Monitor

The modules in a car talk to each other all the time: engine speed, wheel speeds, which doors are open, what the steering wheel buttons are doing. The bus monitor shows those raw messages as they go by, so you can work out which message carries what.

OCTAVE only listens here. The adapter is put in its monitor mode, in which it transmits nothing and does not even acknowledge the messages it hears. There is no way to send a message from OCTAVE.

## Using it

Settings > OBD > Advanced > Bus Monitor, with a vehicle connected. **Listen** starts it, **Stop** ends it.

Each row is one message id:

| Column | Meaning |
|---|---|
| id | The message's identifier, in hex. Three digits on most cars (11-bit CAN), eight on some (29-bit CAN). On a pre-2008 car (J1850, ISO 9141-2, KWP2000) it is the message's three header bytes: priority, receiver, sender. |
| period | How often it comes, in milliseconds. |
| bytes | Its latest data, in hex. A byte that changed within the last second is highlighted. |

To find a signal, do the thing and watch for the byte that moves with it: press the brake, turn the wheel, open a door. Fast-changing bytes at the end of a message are usually a counter or a checksum.

While it listens, the adapter can't answer questions, so the live gauges and the drive recording pause; they resume when you stop. Reading trouble codes, or anything else that asks the car something, stops the monitor.

## Filter

Type one message id next to Listen to follow only that one: `7E8`, or with `X` for "any digit", `0CX` (0C0 to 0CF). Three or eight hex digits. CAN only.

A filter matters because a common adapter sends about 200 messages a second to OCTAVE, and a busy car network has ten times that. The adapter's buffer fills, it drops what it could not send, and OCTAVE counts that as "could not keep up". With a filter the adapter only passes on the id you asked for.

## Recording

**Record to file** writes every message from then until you stop into `history/bus/bus-<date>-<time>.log` in the OCTAVE config folder, one line each:

```
(1731600000.123000) can0 0C9#000C800000000007
```

That is the can-utils log format: `candump`-style tools, SavvyCAN and Wireshark open it. Not available in the browser build.

## What to expect from a car

- On many cars built since about 2010 a gateway sits between the OBD port and the internal networks. The port then shows nothing until something asks a question, and the monitor stays empty. That is the car, not OCTAVE.
- Older CAN cars (roughly 2003 to 2010) and pre-CAN cars often show their traffic at the port.
- `octave --sim` has a simulated bus with six messages that follow the simulated car, enough to try all of the above.

## Limits

- Not on Android yet.
- The adapter stays at 38400 baud, so unfiltered listening on a fast bus loses messages.
- The table keeps the first 1024 ids it sees.
- Listening only. Sending messages is deliberately not part of this.

## How it works

`src/managers/busmonitor.{h,cpp}` is the `busMonitor` object (`available`, `active`, `ids`, `overruns`, `start(filter)`, `stop()`, `startRecording()`). `OBDConnectionWorker::doStartBusMonitor()` sends `ATH1`, `ATCSM1`, `ATCAF0`, an optional `ATCRA<filter>`, then `ATMA`, and reads lines until told to stop; `ELM327Protocol::parseMonitorLine()` turns each into a `BusFrame`. Stopping puts `ATH0`, `ATCAF1` and `ATAR` back. Details: `wiki/obd-manager.html#bus-monitor`. Tests: `tst_obdsim` against the simulated bus, `tst_elm327protocol`.
