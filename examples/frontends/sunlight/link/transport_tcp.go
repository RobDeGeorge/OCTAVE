//go:build !js

package link

import (
	"bufio"
	"net"
	"time"
)

// tcpTransport is the service's TCP side: one JSON message per line.
type tcpTransport struct {
	conn net.Conn
	sc   *bufio.Scanner
}

func dial(addr string, timeout time.Duration) (transport, error) {
	conn, err := net.DialTimeout("tcp", addr, timeout)
	if err != nil {
		return nil, err
	}
	sc := bufio.NewScanner(conn)
	sc.Buffer(make([]byte, 0, 1<<16), 16<<20)
	return &tcpTransport{conn: conn, sc: sc}, nil
}

func (t *tcpTransport) ReadMessage() ([]byte, error) {
	if !t.sc.Scan() {
		if err := t.sc.Err(); err != nil {
			return nil, err
		}
		return nil, errLinkClosed
	}
	return t.sc.Bytes(), nil
}

func (t *tcpTransport) WriteMessage(msg []byte) error {
	_, err := t.conn.Write(append(msg, '\n'))
	return err
}

func (t *tcpTransport) Close() error { return t.conn.Close() }
