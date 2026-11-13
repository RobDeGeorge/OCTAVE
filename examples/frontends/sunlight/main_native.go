//go:build !js

package main

import (
	"errors"
	"flag"
	"io"
	"net"
	"net/http"
	"os"
	"strconv"
	"time"

	"octave/sunlight/link"
)

// canQuit: Ctrl+Q ends the program (and, as the installed screen, OCTAVE).
const canQuit = true

func readOptions() options {
	// OCTAVE sets OCTAVE_HOST / OCTAVE_PORT / OCTAVE_TOKEN when it runs Sunlight as the screen; the flags win
	defHost, defPort := "127.0.0.1", 9876
	if h := os.Getenv("OCTAVE_HOST"); h != "" {
		defHost = h
	}
	if p, err := strconv.Atoi(os.Getenv("OCTAVE_PORT")); err == nil && p > 0 {
		defPort = p
	}
	host := flag.String("host", defHost, "OCTAVE service host (default OCTAVE_HOST, then 127.0.0.1)")
	port := flag.Int("port", defPort, "OCTAVE service port (default OCTAVE_PORT, then 9876)")
	full := flag.Bool("fullscreen", false, "start fullscreen")
	nightMode := flag.Bool("night", false, "start in night mode (white on black)")
	start := flag.String("page", "drive", "drive, dash, media, dtc or trips")
	dashID := flag.String("dash", "", "dashboard id to open on the DASH page")
	token := flag.String("token", os.Getenv("OCTAVE_TOKEN"), "a paired device's token (protocol 2, over the network); default OCTAVE_TOKEN, then the token file")
	tokenFile := flag.String("token-file", link.DefaultTokens().Path, "where tokens are kept, one per host:port (0600)")
	flag.Parse()
	return options{addr: net.JoinHostPort(*host, strconv.Itoa(*port)), night: *nightMode, fullscreen: *full,
		page: *start, dash: *dashID, token: *token, tokens: link.Tokens{Path: *tokenFile}}
}

// fetchArt is the service's GET /art (a 404: no cover).
func fetchArt(addr, auth string) ([]byte, error) {
	cl := http.Client{Timeout: 5 * time.Second}
	req, _ := http.NewRequest(http.MethodGet, "http://"+addr+"/art", nil)
	if auth != "" {
		req.Header.Set("Authorization", auth)
	}
	resp, err := cl.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return nil, errors.New(resp.Status)
	}
	return io.ReadAll(io.LimitReader(resp.Body, 16<<20))
}

// deviceName is how this screen shows in OCTAVE's list of paired devices.
func deviceName() string {
	host, _ := os.Hostname()
	if host == "" {
		return "OCTAVE Sunlight"
	}
	return "OCTAVE Sunlight on " + host
}
