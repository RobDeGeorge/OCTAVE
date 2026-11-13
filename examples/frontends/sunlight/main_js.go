//go:build js

package main

import (
	"errors"
	"fmt"
	"strings"
	"syscall/js"

	"octave/sunlight/link"
)

// canQuit: a page is closed by its tab, not by Ctrl+Q.
const canQuit = false

// try runs fn; a JavaScript exception thrown by a call in it (a missing
// API, a refused fetch) is "no", never a crash.
func try(fn func()) (ok bool) {
	defer func() {
		if recover() != nil {
			ok = false
		}
	}()
	fn()
	return true
}

// readOptions takes the service from the page's own address (OCTAVE serves
// the page and the WebSocket on one port) and the rest from the query:
// ?page=media&night=1&dash=ID.
func readOptions() options {
	o := options{page: "drive", tokens: link.DefaultTokens(), scopes: []string{"media", "vehicle"}}
	try(func() {
		loc := js.Global().Get("location")
		o.addr = loc.Get("host").String()
		q := js.Global().Get("URLSearchParams").New(loc.Get("search"))
		get := func(name string) string {
			if v := q.Call("get", name); v.Type() == js.TypeString {
				return v.String()
			}
			return ""
		}
		if p := get("page"); p != "" {
			o.page = p
		}
		o.dash = get("dash")
		o.night = get("night") != "" && get("night") != "0"
	})
	return o
}

// fetchArt is the service's GET /art, from the host that served the page (a
// 404: no cover). The browser demo's frame has no such host and no cover.
func fetchArt(addr, auth string) (body []byte, err error) {
	if addr == "" {
		return nil, errors.New("no host")
	}
	type result struct {
		body []byte
		err  error
	}
	done := make(chan result, 1)
	var funcs []js.Func
	then := func(fn func(v js.Value) any) js.Func {
		f := js.FuncOf(func(_ js.Value, args []js.Value) any {
			var v js.Value
			if len(args) > 0 {
				v = args[0]
			}
			return fn(v)
		})
		funcs = append(funcs, f)
		return f
	}
	fail := then(func(v js.Value) any {
		select {
		case done <- result{err: errors.New("fetch failed")}:
		default:
		}
		return nil
	})
	// the callbacks go once the whole chain has run, whichever way it went
	var release js.Func
	release = js.FuncOf(func(js.Value, []js.Value) any {
		for _, f := range funcs {
			f.Release()
		}
		release.Release()
		return nil
	})
	ok := try(func() {
		init := js.Global().Get("Object").New()
		init.Set("credentials", "omit")
		init.Set("cache", "no-store")
		if auth != "" {
			headers := js.Global().Get("Object").New()
			headers.Set("Authorization", auth)
			init.Set("headers", headers)
		}
		js.Global().Call("fetch", "/art", init).Call("then", then(func(resp js.Value) any {
			if !resp.Get("ok").Bool() {
				done <- result{err: fmt.Errorf("HTTP %d", resp.Get("status").Int())}
				return nil
			}
			return resp.Call("arrayBuffer")
		})).Call("then", then(func(buf js.Value) any {
			if buf.Type() != js.TypeObject {
				return nil // the reply was not ok: already answered
			}
			arr := js.Global().Get("Uint8Array").New(buf)
			if arr.Length() > 16<<20 {
				done <- result{err: errors.New("cover too large")}
				return nil
			}
			b := make([]byte, arr.Length())
			js.CopyBytesToGo(b, arr)
			done <- result{body: b}
			return nil
		})).Call("catch", fail).Call("then", release)
	})
	if !ok {
		return nil, errors.New("fetch unavailable")
	}
	r := <-done
	return r.body, r.err
}

// deviceName is how this browser shows in OCTAVE's list of paired devices.
func deviceName() string {
	kind := "browser"
	try(func() {
		ua := js.Global().Get("navigator").Get("userAgent").String()
		switch {
		case strings.Contains(ua, "iPhone"), strings.Contains(ua, "iPad"):
			kind = "iPhone"
		case strings.Contains(ua, "Android"):
			kind = "Android"
		}
	})
	return "OCTAVE Sunlight (" + kind + ")"
}
