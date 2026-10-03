"""pyserial URL handlers for OBD adapters, plus the helpers the manager uses
without importing pyserial (it stays a lazy import).

"rfcomm://<MAC>[/<channel>]" is a direct Bluetooth Classic socket, handled by
protocol_rfcomm.py once register_handlers() has run. Mirrors
src/managers/rfcommsocket.{h,cpp} (RfcommSocket::isUrl / parseUrl / makeUrl).
"""

import re

_RFCOMM_URL_RE = re.compile(
    r"^rfcomm://([0-9A-Fa-f]{2}(?::[0-9A-Fa-f]{2}){5})(?:/(\d{1,2}))?/?$",
    re.IGNORECASE)


def is_rfcomm_url(port):
    return bool(port) and port.lower().startswith("rfcomm://")


def parse_rfcomm_url(url):
    """Return (MAC, channel), or None. Channel defaults to 1 (SPP)."""
    m = _RFCOMM_URL_RE.match((url or "").strip())
    if not m:
        return None
    channel = int(m.group(2)) if m.group(2) else 1
    if not 1 <= channel <= 30:
        return None
    return m.group(1).upper(), channel


def make_rfcomm_url(mac, channel=1):
    base = "rfcomm://" + mac.strip().upper()
    return base if channel == 1 else f"{base}/{channel}"


def register_handlers():
    """Let serial.serial_for_url() (python-obd's port opener) find
    protocol_rfcomm. Idempotent."""
    import serial
    if __name__ not in serial.protocol_handler_packages:
        serial.protocol_handler_packages.append(__name__)
