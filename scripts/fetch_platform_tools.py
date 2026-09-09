#!/usr/bin/env python3
"""
Fetch Google's Android platform-tools (adb) for bundling with OCTAVE.

The built-in phone-mirror client needs only `adb` (push, forward, shell).
This downloads a pinned platform-tools release, verifies its SHA-256, and
extracts just the files adb needs into tools/platform-tools/<os>/ together
with Google's NOTICE.txt. Nothing else from the archive is kept.

Usage:
    python scripts/fetch_platform_tools.py            # current platform
    python scripts/fetch_platform_tools.py --all      # linux, windows, darwin
    python scripts/fetch_platform_tools.py --os linux --dest some/dir

The result is gitignored; CI runs this and ships the directory next to the
binary (see .github/workflows/build.yml and packaging/linux/build-appimage.sh).
Google publishes x86_64 builds only; on other architectures (e.g. an ARM
single-board computer) OCTAVE falls back to the distro's adb on PATH.
"""

import argparse
import hashlib
import io
import platform
import sys
import urllib.request
import zipfile
from pathlib import Path

PLATFORM_TOOLS_VERSION = "37.0.1"

# sha256 of platform-tools_r<version>-<os>.zip, recorded when the version was pinned.
ARCHIVES = {
    "linux": ("platform-tools_r{v}-linux.zip",
              "d230f13842f60f782a8645f9c813f8f845bf36089ea7289f28c48f17979313f1"),
    "windows": ("platform-tools_r{v}-win.zip",
                "45f4d63113e895ebde0c90f194099a4676b6ac653bd28d54314a9e022bbc1a99"),
    "darwin": ("platform-tools_r{v}-darwin.zip",
               "ee39ad5967e95c2a07f04dbcbde96b1a0c916ba376096db5d2f498b7727a5d1d"),
}

# Files adb needs at runtime, per platform (paths inside the archive's platform-tools/).
KEEP = {
    "linux": ["adb", "lib64/libc++.so", "NOTICE.txt", "source.properties"],
    "windows": ["adb.exe", "AdbWinApi.dll", "AdbWinUsbApi.dll", "libwinpthread-1.dll",
                "NOTICE.txt", "source.properties"],
    "darwin": ["adb", "NOTICE.txt", "source.properties"],
}

BASE_URL = "https://dl.google.com/android/repository/"


def host_os() -> str:
    return {"Linux": "linux", "Windows": "windows", "Darwin": "darwin"}[platform.system()]


def fetch(os_name: str, dest: Path) -> Path:
    name, sha = ARCHIVES[os_name]
    url = BASE_URL + name.format(v=PLATFORM_TOOLS_VERSION)
    print(f"Downloading {url}")
    data = urllib.request.urlopen(url, timeout=120).read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != sha:
        sys.exit(f"SHA-256 mismatch for {name}: got {digest}, expected {sha}")
    out = dest / os_name
    out.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        for rel in KEEP[os_name]:
            member = "platform-tools/" + rel
            target = out / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            with z.open(member) as src, open(target, "wb") as dst:
                dst.write(src.read())
            if os_name != "windows" and not rel.endswith((".txt", ".properties", ".so")):
                target.chmod(0o755)
    print(f"platform-tools {PLATFORM_TOOLS_VERSION} ({os_name}) -> {out}")
    return out


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--os", choices=sorted(ARCHIVES), help="target OS (default: this machine)")
    ap.add_argument("--all", action="store_true", help="fetch every platform")
    ap.add_argument("--dest", type=Path, default=Path(__file__).resolve().parent.parent / "tools" / "platform-tools")
    args = ap.parse_args()
    targets = sorted(ARCHIVES) if args.all else [args.os or host_os()]
    for os_name in targets:
        fetch(os_name, args.dest)


if __name__ == "__main__":
    main()
