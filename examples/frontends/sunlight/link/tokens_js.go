//go:build js

package link

import "syscall/js"

// Tokens keeps this browser's token in localStorage (one origin is one
// OCTAVE, so there is one key). Storage that throws (a private window, the
// browser demo's sandboxed frame) means no token is kept: the page pairs
// again next time.
type Tokens struct{ Path string }

const tokenKey = "sunlight.token"

// storage is the page's localStorage. Reading the property itself throws in
// a sandboxed frame, and only a call (not a property read) turns a JavaScript
// exception into a Go panic that recover can catch: hence Reflect.get.
func storage() js.Value {
	return js.Global().Get("Reflect").Call("get", js.Global(), "localStorage")
}

// DefaultTokens is the page's localStorage.
func DefaultTokens() Tokens { return Tokens{Path: "localStorage"} }

// Get returns the stored token, or "".
func (t Tokens) Get(addr string) (token string) {
	defer func() {
		if recover() != nil {
			token = ""
		}
	}()
	v := storage().Call("getItem", tokenKey)
	if v.Type() != js.TypeString {
		return ""
	}
	return v.String()
}

// Put stores (or, with "", forgets) the token.
func (t Tokens) Put(addr, token string) (err error) {
	defer func() { _ = recover() }() // not kept: nothing to report
	store := storage()
	if token == "" {
		store.Call("removeItem", tokenKey)
	} else {
		store.Call("setItem", tokenKey, token)
	}
	return nil
}
