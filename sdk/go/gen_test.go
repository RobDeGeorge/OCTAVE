package octave

import (
	"bytes"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"testing"
)

// The generated files match docs/api (so they go stale loudly, like
// tst_apidocs), and annotations come through as Go doc conventions.
func runGen(t *testing.T, api string) map[string][]byte {
	t.Helper()
	out := t.TempDir()
	cmd := exec.Command("go", "run", "./internal/gen", "-api", api, "-out", out)
	cmd.Env = append(os.Environ(), "GOFLAGS=-mod=mod")
	if b, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("generator: %v\n%s", err, b)
	}
	files := map[string][]byte{}
	for _, name := range []string{"protocol_gen.go", "objects_gen.go"} {
		b, err := os.ReadFile(filepath.Join(out, name))
		if err != nil {
			t.Fatal(err)
		}
		files[name] = b
	}
	return files
}

func TestGeneratedFilesAreCurrent(t *testing.T) {
	fresh := runGen(t, filepath.Join("..", "..", "docs", "api"))
	for name, want := range fresh {
		have, err := os.ReadFile(name)
		if err != nil {
			t.Fatal(err)
		}
		if !bytes.Equal(have, want) {
			t.Errorf("%s is stale against docs/api: run `go generate ./...` and commit it", name)
		}
	}
}

func TestAnnotationsBecomeGoDoc(t *testing.T) {
	// The TypeScript SDK's fixture: an API with deprecated and experimental members
	fixture := filepath.Join("..", "typescript", "test", "fixture-api")
	if _, err := os.Stat(filepath.Join(fixture, "index.json")); err != nil {
		t.Skip("no fixture API")
	}
	// The fixture has no protocol.json; the real one serves
	dir := t.TempDir()
	for _, name := range []string{"index.json", "demo.json", "old.json"} {
		b, _ := os.ReadFile(filepath.Join(fixture, name))
		os.WriteFile(filepath.Join(dir, name), b, 0o644)
	}
	b, _ := os.ReadFile(filepath.Join("..", "..", "docs", "api", "protocol.json"))
	os.WriteFile(filepath.Join(dir, "protocol.json"), b, 0o644)
	src := string(runGen(t, dir)["objects_gen.go"])
	for _, want := range []string{
		`// Deprecated: use title; goes in protocol 3\nfunc \(o Demo\) GetName\(\)`,
		`// Deprecated: use title; goes in protocol 3\nfunc \(o Demo\) SetName\(`,
		`// Experimental: [^\n]*\nfunc \(o Demo\) GetLevel\(\)`,
		`// Deprecated: use run\(speed, mode\); goes in protocol 3\nfunc \(o Demo\) Go\(speed int\) error`,
		`func \(o Demo\) Run\(speed int, mode string\) \(v bool, err error\)`,
		`// Experimental: [^\n]*\ntype Old struct`,
	} {
		if !regexp.MustCompile(want).MatchString(src) {
			t.Errorf("objects_gen.go lacks %q", want)
		}
	}
	if regexp.MustCompile(`Deprecated:[^\n]*\nfunc \(o Demo\) Run\(`).MatchString(src) {
		t.Error("Run is not deprecated")
	}
}
