"""Start the console the way OCTAVE starts an installed frontend.

    python3 launch.py [console options]        # what octave-frontend.json runs

- Uses the folder's .venv when there is one (a checkout), else vendor/ beside
  this file (an installed copy: `pip install --target vendor`, see README).
- In a terminal it simply runs `python -m octave_console`.
- Started by OCTAVE as the screen there is no terminal (its output goes to
  OCTAVE's log), so it checks the token with one hello, logs the result, and
  opens a full-screen terminal emulator running the console. The console's
  exit code is passed back: 0 (Q) quits OCTAVE, anything else restarts it.
  OCTAVE_CONSOLE_TERMINAL picks the emulator ("xterm -fullscreen -e");
  else the first of kitty, foot, alacritty, gnome-terminal, konsole,
  xfce4-terminal, xterm on PATH.
"""

import json
import os
import shlex
import shutil
import signal
import socket
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TITLE = "OCTAVE Console"
TERMINALS = [  # (program, arguments before the command)
    ("kitty", ["--start-as=fullscreen", "--title", TITLE]),
    ("foot", ["--fullscreen", "--title", TITLE]),
    ("alacritty", ["--title", TITLE, "-o", 'window.startup_mode="Fullscreen"', "-e"]),
    ("gnome-terminal", ["--full-screen", "--wait", "--"]),
    ("konsole", ["--fullscreen", "-e"]),
    ("xfce4-terminal", ["--fullscreen", "--disable-server", "-x"]),
    ("xterm", ["-fullscreen", "-T", TITLE, "-e"]),
]


def log(text):
    print(f"console: {text}", flush=True)


def use_venv():
    """Re-run under the checkout's .venv when there is one and this isn't it"""
    for rel in ("bin/python", "Scripts/python.exe"):
        py = os.path.join(HERE, ".venv", rel)
        if os.path.exists(py) and os.path.realpath(sys.prefix) != os.path.realpath(os.path.join(HERE, ".venv")):
            os.execv(py, [py, os.path.abspath(__file__)] + sys.argv[1:])


def setup_path():
    vendor = os.path.join(HERE, "vendor")
    if os.path.isdir(vendor):
        sys.path.insert(0, vendor)
        if os.path.isdir(os.path.join(vendor, "octave_client")):
            os.environ.setdefault("OCTAVE_SDK", vendor)
    sys.path.insert(0, HERE)


def check_token():
    """One hello with OCTAVE's token, for the log"""
    host, port = os.environ.get("OCTAVE_HOST", "127.0.0.1"), int(os.environ.get("OCTAVE_PORT") or 9876)
    token = os.environ.get("OCTAVE_TOKEN")
    try:
        with socket.create_connection((host, port), timeout=5) as s:
            hello = {"op": "hello", "id": 1}
            if token:
                hello["token"] = token
            s.sendall((json.dumps(hello) + "\n").encode())
            reply = json.loads(s.makefile().readline())
        r = reply.get("result") or {}
        log(f"OCTAVE {r.get('app', {}).get('version', '?')} at {host}:{port}, protocol {r.get('protocolVersion')}, "
            f"{'with a token' if token else 'no token'}: {r.get('auth', 'trusted')}"
            + (" (token rejected)" if r.get("tokenRejected") else ""))
    except (OSError, ValueError) as e:
        log(f"no answer from {host}:{port} yet ({e}); the console retries by itself")


def terminal():
    chosen = os.environ.get("OCTAVE_CONSOLE_TERMINAL")
    if chosen:
        return shlex.split(chosen)
    for program, args in TERMINALS:
        path = shutil.which(program)
        if path:
            return [path] + args
    return None


def in_terminal():
    """Run inside the emulator: the console, its exit code written for the launcher outside"""
    status = os.environ.pop("OCTAVE_CONSOLE_STATUS", "")
    code = 1
    try:
        from octave_console.__main__ import main
        main()
        code = 0
    except SystemExit as e:
        code = e.code if isinstance(e.code, int) else (0 if e.code is None else 1)
    except BaseException:
        import traceback
        if status:
            with open(status + ".err", "w") as fh:
                traceback.print_exc(file=fh)
        raise
    finally:
        if status:
            with open(status, "w") as fh:
                fh.write(str(code))
    return code


def main():
    use_venv()
    setup_path()
    if os.environ.get("OCTAVE_CONSOLE_STATUS"):
        return in_terminal()
    # octave_console/link.py finds the SDK in OCTAVE_SDK (vendor/ above) or in the checkout
    sdk = os.environ.get("OCTAVE_SDK") or os.path.join(HERE, "..", "..", "..", "sdk", "python")
    try:
        import textual  # noqa: F401
        missing = None if os.path.isdir(os.path.join(sdk, "octave_client")) else "no octave_client (sdk/python)"
    except ImportError as e:
        missing = str(e)
    if missing:
        log(f"{missing}: fill vendor/ first (README, 'Install it as the screen')")
        return 1
    if sys.stdout.isatty():
        from octave_console.__main__ import main as console
        console()
        return 0

    # Started as the screen: no terminal of our own
    check_token()
    term = terminal()
    if not term:
        log("no terminal emulator found (kitty, foot, alacritty, gnome-terminal, konsole, xfce4-terminal, xterm); "
            "set OCTAVE_CONSOLE_TERMINAL")
        return 1
    with tempfile.TemporaryDirectory(prefix="octave-console-") as tmp:
        status = os.path.join(tmp, "status")
        env = dict(os.environ, OCTAVE_CONSOLE_STATUS=status)
        cmd = term + [sys.executable, os.path.abspath(__file__)] + sys.argv[1:]
        log(f"running in {os.path.basename(term[0])}")
        child = subprocess.Popen(cmd, env=env, cwd=HERE)

        def stop(signum, _frame):
            # OCTAVE ends its frontend with SIGTERM: take the terminal (and the console in it) along
            child.terminate()
            try:
                child.wait(5)
            except subprocess.TimeoutExpired:
                child.kill()
            sys.exit(128 + signum)

        for sig in (signal.SIGTERM, signal.SIGINT, getattr(signal, "SIGHUP", None)):
            if sig is not None:
                signal.signal(sig, stop)
        rc = child.wait()
        if os.path.exists(status + ".err"):
            with open(status + ".err") as fh:
                for line in fh.read().splitlines()[-12:]:
                    log(line)
        try:
            with open(status) as fh:
                rc = int(fh.read().strip() or 1)
        except (OSError, ValueError):
            log(f"the terminal closed without the console's exit code ({rc})")
            rc = rc or 1
        log(f"exit code {rc}")
        return rc


if __name__ == "__main__":
    sys.exit(main())
