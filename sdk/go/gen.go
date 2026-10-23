package octave

// protocol_gen.go and objects_gen.go come from docs/api (protocol.json and
// the object files). Regenerate after `octave --dump-api docs/api` or an
// edit to protocol.json; gen_test.go fails while they are stale.
//go:generate go run ./internal/gen -api ../../docs/api -out .
