package link

import (
	"encoding/json"
	"os"
	"path/filepath"
)

// Tokens keeps paired devices' tokens, one per host:port, in a JSON file only
// the user can read (0600 in a 0700 directory).
type Tokens struct{ Path string }

// DefaultTokens is $XDG_CONFIG_HOME/octave-sunlight/tokens.json (or ~/.config/...).
func DefaultTokens() Tokens {
	base := os.Getenv("XDG_CONFIG_HOME")
	if base == "" {
		home, _ := os.UserHomeDir()
		base = filepath.Join(home, ".config")
	}
	return Tokens{Path: filepath.Join(base, "octave-sunlight", "tokens.json")}
}

func (t Tokens) load() map[string]string {
	m := map[string]string{}
	if raw, err := os.ReadFile(t.Path); err == nil {
		_ = json.Unmarshal(raw, &m)
	}
	return m
}

// Get returns the token for addr, or "".
func (t Tokens) Get(addr string) string { return t.load()[addr] }

// Put stores (or, with "", forgets) addr's token.
func (t Tokens) Put(addr, token string) error {
	m := t.load()
	if token == "" {
		delete(m, addr)
	} else {
		m[addr] = token
	}
	if err := os.MkdirAll(filepath.Dir(t.Path), 0o700); err != nil {
		return err
	}
	_ = os.Chmod(filepath.Dir(t.Path), 0o700)
	raw, _ := json.MarshalIndent(m, "", " ")
	if err := os.WriteFile(t.Path, raw, 0o600); err != nil {
		return err
	}
	return os.Chmod(t.Path, 0o600)
}
