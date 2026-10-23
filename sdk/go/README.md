# octave (Go)

A client for OCTAVE's service API in Go, standard library only, generated from [`docs/api/`](../../docs/api/README.md). It exists to prove the published spec is not TypeScript-shaped: `protocol_gen.go` comes from `protocol.json` (the ops with their arguments and replies, the events, the error codes, the stream record constants, the routes) and `objects_gen.go` from the object files (one Go type per service object, a method per API method, `Get` / `Set` per property). `client.go`, the hand-written part, is the line protocol over TCP: requests matched to replies by id, events and media records on channels.

```bash
./build/octave --headless            # or --service with the UI; a developer build adds --dev for a simulated car
cd sdk/go && go test ./...           # the generated files are current, and the client against a headless app
```

## Use

```go
import octave "github.com/RobDeGeorge/OCTAVE/sdk/go"

c, err := octave.Dial("127.0.0.1:9876")          // says hello, checks the protocol version
defer c.Close()

vol := octave.NewVolumeController(c)              // typed from docs/api/volumeController.json
err = vol.ApplyVolume(40)
percent, err := vol.GetPercent()
muted, err := vol.SetMuted(true)

sub, err := c.Subscribe(octave.SubscribeArgs{Object: "paramStore", Properties: []string{"RPM", "SPEED"}})
for ev := range c.Events {                        // every subscription event, as it arrives
    if ev.Kind == octave.EventParams {
        var p octave.ParamsEvent
        ev.Decode(&p)
        fmt.Println(p.Values)                     // {"RPM": 2210, "SPEED": 41}, native units
    }
}
```

`Client` has `Request(op, args, &out)` for any op (the generated `<Op>Args` types fit, or a map), `Describe` / `DescribeAll` (which also lists the server's ops), `Get`, `Set`, `Call`, `Rows`, `Subscribe` / `Unsubscribe`, `Stream` / `Unstream` (records on `c.Records`), `Pair(code, name, scopes...)` / `Unpair` / `MediaTicket`, `Trusted()` and `Scopes()` (what the device may change: `Pair`'s or `hello`'s grant). A failure reply is an `*octave.Error` with the message and, when the server gives one, the code (`octave.CodeUnauthorised`). Read `Events` and `Records`, or the reader blocks once they fill. `WithToken`, `WithTimeout` and `WithoutVersionCheck` are the options.

Overloaded API methods get their argument count as a suffix (`RequestSeries3`, `RequestSeries4`, `RequestSeries5`); a method whose Go name would collide with a property getter gets `Call` appended (`AudioAnalyzer.GetNumBarsCall`). A member the API marks deprecated carries a `Deprecated:` paragraph, so editors and `staticcheck` flag its use; an experimental one an `Experimental:` paragraph. Qt types map to `string`, `bool`, `int`, `int64`, `float64`, `[]string`, `[]any`, `map[string]any` and `any` (enums and QVariants); a list-model property reads as `ModelSummary`.

## Generated code

`go generate ./...` (or `go run ./internal/gen -api ../../docs/api -out .`) rewrites both files after `octave --dump-api docs/api` or an edit to `protocol.json`. `gen_test.go` fails while they are stale, and checks the `Deprecated:` / `Experimental:` paragraphs against the TypeScript SDK's fixture API (`sdk/typescript/test/fixture-api`). `client_test.go` runs against the binary in `OCTAVE_BIN` (ctest sets it, as `sdk_go`; the test skips without one) on a throwaway config.

## Known gaps

- Line protocol only (no WebSocket; a Go program needs none).
- Signals have no typed wrappers: subscribe by name and decode `SignalEvent`.
- No reconnect: `Dial` again when `Events` closes.
