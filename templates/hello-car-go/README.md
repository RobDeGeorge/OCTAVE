# Hello, car (Go)

The smallest OCTAVE frontend in Go: two live readings, the track with play / pause, the cover saved to a file, and pairing when this machine is not the head unit. One file of about 170 lines on OCTAVE's Go SDK (`sdk/go`, a module dependency, standard library only). Copy the folder and start from it.

## Run

```bash
./build/octave --headless --sim            # a simulated car, no window (any build; or --service with the UI)
cd templates/hello-car-go && go run .
```

Enter toggles play / pause, Ctrl-C quits. From another machine, start OCTAVE with `--service-bind 0.0.0.0` and run `go run . <the head unit's address>`: it asks for the 6-digit code from Settings > Service > Pair a device, asks only for the **media** permission (play / pause; readings are always allowed), and keeps the token in `.token` in the working directory for next time.

Make the numbers move with the simulator: `octave.NewSim(c).SetThrottle(40)`, or a scenario (`docs/api/README.md`, "Simulator").

**The dependency.** `go.mod` requires `github.com/RobDeGeorge/OCTAVE/sdk/go` and, while this folder sits inside the OCTAVE checkout, points it at `../../sdk/go` with a `replace` line so it builds from the same tree. When you copy the folder out, delete the `replace` line and `go get github.com/RobDeGeorge/OCTAVE/sdk/go@main` (or keep a `replace` to wherever your copy of the SDK is).

## What it uses

| | |
|---|---|
| `octave.Dial(addr, octave.WithToken(t))`, `c.Pair(code, name, "media")`, `c.MediaTicket()` | connect, pair, a ticket for the cover URL |
| `octave.NewParameterCatalog(c).Info(id)` | the unit and decimals of each reading |
| `octave.NewObdManager(c).SetParameterDemand("hello-car", pids)` | ask OCTAVE to poll these two; `[]` when done |
| `c.Subscribe(...)`, `c.Events`, `octave.ParamsEvent` | the values, native units |
| `nowPlaying` subscription, `octave.NewNowPlaying(c).TogglePlay()` | the track and the key |
| `GET /art?rev=N&ticket=T` | the cover, saved as `cover.<type>` |

## Check it

```bash
cd templates/hello-car-go && OCTAVE_BIN=../../build/octave go test ./...
```

Starts `octave --headless --sim` on a free port with a throwaway config, builds the program and runs it with `-check 4s`, requiring readings and a playing track. ctest runs it as `template_go` when CMake finds `go`.

## Next

`docs/api/<object>.json` lists every object's members (each is a Go type in the SDK), `docs/api/protocol.json` the protocol, `wiki/writing-a-frontend.html` walks through it.
