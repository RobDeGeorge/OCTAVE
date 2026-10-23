// Hello, car: the smallest OCTAVE frontend in Go. Connects, pairs when this
// machine is not the head unit (asking only for the media permission), shows
// RPM and speed live, the track with play / pause on Enter, and saves the
// cover next to the binary.
//
//	go run .                       # 127.0.0.1:9876
//	go run . 192.168.1.20          # the head unit over the network: pairs once, keeps the token
//	go run . -check 5s             # run unattended and exit 0 if everything arrived (the test)
//
// The client is OCTAVE's Go SDK (sdk/go), a module dependency.
package main

import (
	"bufio"
	"flag"
	"fmt"
	"io"
	"net/http"
	"os"
	"strings"
	"time"

	octave "github.com/RobDeGeorge/OCTAVE/sdk/go"
)

const name = "hello-car" // this frontend's name: its demand consumer and its device name

var pids = []string{"RPM", "SPEED"}

func main() {
	check := flag.Duration("check", 0, "unattended: toggle play, run this long, exit 0 if readings and a track arrived")
	port := flag.Int("port", 9876, "the service port")
	flag.Parse()
	host := "127.0.0.1"
	if flag.NArg() > 0 {
		host = flag.Arg(0)
	}
	addr := fmt.Sprintf("%s:%d", host, *port)

	c, err := connect(addr)
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	defer c.Close()
	fmt.Printf("OCTAVE %s, protocol %d\n", c.Hello.App.Version, c.Hello.ProtocolVersion)

	// Readings: titles and units from the catalog, values from paramStore (native units).
	// OCTAVE polls the car only for what someone shows: ask for ours, by our own consumer name.
	catalog := octave.NewParameterCatalog(c)
	info := map[string]map[string]any{}
	for _, pid := range pids {
		info[pid], _ = catalog.Info(pid)
	}
	obd := octave.NewObdManager(c)
	obd.SetParameterDemand(name, pids)
	defer obd.SetParameterDemand(name, []string{}) // done showing them (a closed connection does this too)
	c.Subscribe(octave.SubscribeArgs{Object: "paramStore", Properties: pids, ThrottleMs: octave.Int(200)})
	c.Subscribe(octave.SubscribeArgs{Object: "nowPlaying", Properties: []string{"hasTrack", "title", "playing", "artRevision"}, ThrottleMs: octave.Int(250)})

	player := octave.NewNowPlaying(c)
	pressed := false
	if *check == 0 {
		fmt.Println("Enter toggles play / pause, Ctrl-C quits.")
		go func() {
			for s := bufio.NewScanner(os.Stdin); s.Scan(); {
				player.TogglePlay()
			}
		}()
	}

	values := map[string]*float64{}
	track := map[string]any{}
	cover := ""
	deadline := time.Now().Add(*check)
	timeout := time.NewTimer(*check)
	if *check == 0 {
		timeout.Stop()
	}
loop:
	for {
		select {
		case <-timeout.C:
			break loop
		case ev, ok := <-c.Events:
			if !ok {
				break loop
			}
			switch ev.Kind {
			case octave.EventParams:
				var p octave.ParamsEvent
				ev.Decode(&p)
				for id, v := range p.Values {
					values[id] = v
				}
			case octave.EventProperty:
				var p octave.PropertyEvent
				ev.Decode(&p)
				if p.Object != "nowPlaying" {
					continue
				}
				track[p.Name] = p.Value
				if p.Name == "artRevision" && track["hasTrack"] == true {
					cover = fetchCover(c, addr, p.Value)
				}
				if *check > 0 && p.Name == "playing" && p.Value == false && !pressed {
					pressed = true
					player.TogglePlay() // press Play once (nothing loaded yet starts the first track)
				}
			default:
				continue
			}
			var readings []string
			for _, pid := range pids {
				if v := values[pid]; v != nil {
					readings = append(readings, fmt.Sprintf("%s %.*f %v", pid, int(info[pid]["decimals"].(float64)), *v, info[pid]["unit"]))
				}
			}
			state := "nothing playing"
			if track["hasTrack"] == true {
				state = map[bool]string{true: "playing", false: "paused"}[track["playing"] == true]
			}
			title, _ := track["title"].(string)
			line := fmt.Sprintf("%-32s | %s: %s", strings.Join(readings, "  "), state, title)
			if cover != "" {
				line += "  cover: " + cover
			}
			fmt.Printf("\r%-118.118s", line)
		}
		if *check > 0 && time.Now().After(deadline) {
			break
		}
	}
	fmt.Println()
	if *check > 0 {
		_, sawSpeed := values["SPEED"]
		ok := values["RPM"] != nil && *values["RPM"] > 0 && sawSpeed && track["hasTrack"] == true && track["playing"] == true
		if !ok {
			fmt.Printf("hello-car-go: FAIL values=%v track=%v\n", values, track)
			os.Exit(1)
		}
		fmt.Println("hello-car-go: all checks passed")
	}
}

// connect returns a trusted client: on the head unit at once, elsewhere after
// pairing with the code the user reads off OCTAVE's screen.
func connect(addr string) (*octave.Client, error) {
	tokenFile := ".token"
	token, _ := os.ReadFile(tokenFile)
	c, err := octave.Dial(addr, octave.WithToken(strings.TrimSpace(string(token))))
	if err != nil {
		return nil, err
	}
	if c.Hello.TokenRejected {
		os.Remove(tokenFile) // revoked or unknown: pair again
	}
	in := bufio.NewReader(os.Stdin)
	for !c.Trusted() {
		fmt.Println("This machine is not the head unit. On OCTAVE open Settings > Service > Pair a device.")
		fmt.Print("The 6-digit code: ")
		code, _ := in.ReadString('\n')
		tok, err := c.Pair(strings.TrimSpace(code), name, "media") // play / pause only; readings are always allowed
		if err != nil {
			fmt.Println(" ", err)
			continue
		}
		os.WriteFile(tokenFile, []byte(tok), 0o600)
		fmt.Printf("Paired with %v; the token is in %s\n", c.Scopes(), tokenFile)
	}
	return c, nil
}

// fetchCover gets /art with a short-lived media ticket in the URL (never the
// token) and saves it; "" when the track has no cover.
func fetchCover(c *octave.Client, addr string, revision any) string {
	t, err := c.MediaTicket()
	if err != nil {
		return ""
	}
	resp, err := http.Get(fmt.Sprintf("http://%s/art?rev=%v&ticket=%s", addr, revision, t.Ticket))
	if err != nil || resp.StatusCode != 200 {
		return ""
	}
	defer resp.Body.Close()
	kind := strings.TrimPrefix(resp.Header.Get("Content-Type"), "image/")
	path := "cover." + kind
	f, err := os.Create(path)
	if err != nil {
		return ""
	}
	defer f.Close()
	io.Copy(f, resp.Body)
	return path
}
