"""Make a Windows deploy folder self-contained: copy in every DLL that any
packaged .exe/.dll imports and that a clean Windows install doesn't provide.

windeployqt handles Qt, and CI copies a few vcpkg DLLs by name, but transitive
dependencies slipped through: tag.dll imports z.dll (zlib), which was on the
build machine's PATH, so every installer up to 0.9.4 failed to start on a clean
PC with "z.dll was not found". This walks the import tables instead of trusting
a hand-kept list.

Usage: python resolve_dlls.py <deploy_dir> <search_dir> [<search_dir> ...]

A DLL counts as provided when it's next to the binary that imports it (a
helper exe such as platform-tools\\adb.exe loads its own AdbWinApi.dll from
there), in <deploy_dir> (the app folder, which Windows searches for the main
exe and every plugin it loads), or in C:\\Windows\\System32, except for the Visual
C++ runtime family, which the build runner has in System32 but a clean PC
doesn't, so those must be in <deploy_dir>. API-set stubs (api-ms-*, ext-ms-*)
are always provided by the OS. Missing DLLs are copied from the search dirs
(vcpkg bin, Qt bin), and the scan repeats until nothing new is added. Exits 1
if anything is still unresolved.
"""
import os
import shutil
import sys

import pefile

SYSTEM32 = os.path.join(os.environ.get("SystemRoot", r"C:\Windows"), "System32")
# Redistributable runtimes: present on build runners, absent on a clean PC.
MUST_SHIP_PREFIXES = ("msvcp1", "vcruntime1", "concrt1", "vccorlib1", "vcomp1")


def imports_of(path):
    try:
        pe = pefile.PE(path, fast_load=True)
        pe.parse_data_directories(directories=[
            pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"],
            pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT"],
        ])
    except pefile.PEFormatError:
        return set()
    names = set()
    for attr in ("DIRECTORY_ENTRY_IMPORT", "DIRECTORY_ENTRY_DELAY_IMPORT"):
        for entry in getattr(pe, attr, []) or []:
            names.add(entry.dll.decode("ascii", "replace").lower())
    pe.close()
    return names


def main():
    deploy = os.path.abspath(sys.argv[1])
    search = [os.path.abspath(d) for d in sys.argv[2:] if os.path.isdir(d)]
    copied, unresolved = [], set()
    while True:
        present = {f.lower() for f in os.listdir(deploy)}
        binaries = []
        for root, _dirs, files in os.walk(deploy):
            binaries += [os.path.join(root, f) for f in files
                         if f.lower().endswith((".exe", ".dll"))]
        wanted = set()
        for b in binaries:
            beside = {f.lower() for f in os.listdir(os.path.dirname(b))}
            wanted |= {d for d in imports_of(b) if d not in beside}
        added = False
        unresolved = set()
        for dll in sorted(wanted):
            if dll.startswith(("api-ms-", "ext-ms-")) or dll in present:
                continue
            if not dll.startswith(MUST_SHIP_PREFIXES) and os.path.exists(os.path.join(SYSTEM32, dll)):
                continue
            src = next((os.path.join(d, f) for d in search for f in os.listdir(d)
                        if f.lower() == dll), None)
            if src:
                shutil.copy2(src, deploy)
                copied.append(f"{dll}  <- {src}")
                added = True
            else:
                unresolved.add(dll)
        if not added:
            break
    for line in copied:
        print("copied", line)
    if unresolved:
        print("UNRESOLVED (not in the deploy dir, System32 or any search dir):")
        for dll in sorted(unresolved):
            print("  ", dll)
        sys.exit(1)
    print(f"All DLL imports resolved ({len(copied)} copied).")


if __name__ == "__main__":
    main()
