"""pyserial URL handler for "rfcomm://<MAC>[/<channel>]": a direct Bluetooth
Classic RFCOMM socket to an ELM327 (Linux only), so python-obd can talk to a
dongle without `rfcomm bind` (root) or a /dev/rfcommN node. Connecting is
explicit, with a timeout and a real error ("Host is down"), and nothing else
on the system (blueman) can release the link underneath us. Mirrors
src/managers/rfcommsocket.{h,cpp}.

python-obd opens its port with serial.serial_for_url(), which finds this
module once backend.obd_transports.register_handlers() has run.
"""

import errno
import socket
import time

from serial.serialutil import SerialException
from serial.urlhandler import protocol_socket

from backend.obd_transports import parse_rfcomm_url

CONNECT_TIMEOUT_S = 10


class Serial(protocol_socket.Serial):
    """protocol_socket's select()-based read/write work unchanged on an
    RFCOMM socket; only the address and the connect differ."""

    def open(self):
        self.logger = None
        if self._port is None:
            raise SerialException("Port must be configured before it can be used.")
        if self.is_open:
            raise SerialException("Port is already open.")
        mac, channel = self.from_url(self.portstr)
        if not hasattr(socket, "AF_BLUETOOTH"):
            raise SerialException("Direct Bluetooth (rfcomm://) is only supported on Linux")
        deadline = time.monotonic() + 3
        while True:
            sock = socket.socket(socket.AF_BLUETOOTH, socket.SOCK_STREAM, socket.BTPROTO_RFCOMM)
            try:
                sock.settimeout(CONNECT_TIMEOUT_S)
                sock.connect((mac, channel))
                break
            except OSError as e:
                sock.close()
                # EBUSY: one link per channel, and a previous connection (or a
                # process holding /dev/rfcommN open) still has it; switching
                # transports releases it on another thread, so give it a moment
                if e.errno == errno.EBUSY and time.monotonic() < deadline:
                    time.sleep(0.25)
                    continue
                reason = e.strerror or str(e) or "timed out"
                if e.errno == errno.EBUSY:
                    reason += " (another connection, e.g. an open /dev/rfcomm node, holds the adapter)"
                raise SerialException(f"Bluetooth connect failed: {reason}") from e
        sock.setblocking(False)
        self._socket = sock
        self._reconfigure_port()
        self.is_open = True
        self.reset_input_buffer()

    def from_url(self, url):
        parsed = parse_rfcomm_url(url)
        if parsed is None:
            raise SerialException(
                f"expected rfcomm://<MAC>[/<channel>], got {url!r}")
        return parsed
