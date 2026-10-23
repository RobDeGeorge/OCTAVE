// Follow the track and the volume from the terminal: the Go client in 40
// lines. `go run ./examples/now_playing [host:port]` against a running
// `octave --service` (or --headless).
package main

import (
	"fmt"
	"os"

	octave "github.com/RobDeGeorge/OCTAVE/sdk/go"
)

func main() {
	addr := "127.0.0.1:9876"
	if len(os.Args) > 1 {
		addr = os.Args[1]
	}
	c, err := octave.Dial(addr)
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	defer c.Close()
	fmt.Printf("OCTAVE %s, protocol %d\n", c.Hello.App.Version, c.Hello.ProtocolVersion)
	if _, err := c.Subscribe(octave.SubscribeArgs{Object: "nowPlaying", Properties: []string{"title", "artist", "playing"}}); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	if _, err := c.Subscribe(octave.SubscribeArgs{Object: "volumeController", Properties: []string{"percent"}}); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	for ev := range c.Events {
		if ev.Kind != octave.EventProperty {
			continue
		}
		var p octave.PropertyEvent
		if ev.Decode(&p) == nil {
			fmt.Printf("%s.%s = %v\n", p.Object, p.Name, p.Value)
		}
	}
}
