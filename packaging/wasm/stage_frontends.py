#!/usr/bin/env python3
"""Stage the web frontends the browser build offers, and their catalog.

    python3 packaging/wasm/stage_frontends.py OUT_DIR

Copies each built web frontend below into OUT_DIR/frontends/<slug>/ and
writes OUT_DIR/frontends/catalog.json: one entry per staged frontend, made
from its own octave-frontend.json (id, name, version, description, protocol,
scopes) plus `slug`, `path` (its folder, relative to the site) and
`preview` (its picture for the chooser, when it has one). The
browser build lists them in Settings > Device > Frontends (index.html,
src/platform/wasm/webfrontends.h) and the playground in its picker
(playground.html); `?frontend=<slug>` picks one on either page.

frontends/native.json lists the example programs that have no browser
build (same keys, `native: true`, no `path`): the chooser shows them greyed
out with their picture.

A frontend that has not been built (no index.html) is left out with a
warning, so a site without Node still builds.
"""

import json
import shutil
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[2]

# slug -> the folder that is served (index.html and octave-frontend.json in it)
FRONTENDS = {
    "hello-car-js": SRC / "templates" / "hello-car-js",
    "glass": SRC / "examples" / "frontends" / "glass" / "dist",
    # Native frontends built for the browser as well (each one's build_web.sh)
    "sunlight": SRC / "examples" / "frontends" / "sunlight" / "dist-web",
    "arcade": SRC / "examples" / "frontends" / "arcade" / "dist-web",
    "retro-cluster": SRC / "examples" / "frontends" / "retro-cluster" / "dist-web",
}
# slug -> its picture for the chooser (the manifest's `preview`, 640 x 400),
# copied to <slug>/preview.png whether or not the built folder carries it
EXAMPLES = SRC / "examples" / "frontends"
PREVIEWS = {
    "glass": EXAMPLES / "glass" / "public" / "preview.png",
    "sunlight": EXAMPLES / "sunlight" / "preview.png",
    "arcade": EXAMPLES / "arcade" / "preview.png",
    "retro-cluster": EXAMPLES / "retro-cluster" / "preview.png",
}
# Programs with no browser build: listed in the chooser with their picture,
# greyed out (frontends/native.json), so the page shows what a head unit can run
NATIVE = {
    "console": EXAMPLES / "console",
}
# The template's own folder also has its README / LICENSE; only these are served
ONLY = {"hello-car-js": ["index.html", "app.js", "octave.js", "octave-frontend.json"]}
KEYS = ("id", "name", "version", "description", "protocol", "scopes")


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = Path(sys.argv[1]) / "frontends"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    catalog = []
    for slug, folder in FRONTENDS.items():
        manifest_file = folder / "octave-frontend.json"
        if not (folder / "index.html").is_file() or not manifest_file.is_file():
            print(f"stage_frontends: {folder} is not built; {slug} left out", file=sys.stderr)
            continue
        manifest = json.loads(manifest_file.read_text(encoding="utf-8"))
        if "web" not in manifest:
            sys.exit(f"stage_frontends: {manifest_file} is not a web frontend")
        if slug in ONLY:
            (out / slug).mkdir()
            for name in ONLY[slug]:
                shutil.copy2(folder / name, out / slug / name)
        else:
            shutil.copytree(folder, out / slug)
        entry = {k: manifest[k] for k in KEYS if k in manifest}
        entry.update(slug=slug, path=f"frontends/{slug}/")
        if slug in PREVIEWS and PREVIEWS[slug].is_file():
            shutil.copy2(PREVIEWS[slug], out / slug / "preview.png")
            entry["preview"] = f"frontends/{slug}/preview.png"
        catalog.append(entry)
    native = []
    for slug, folder in NATIVE.items():
        manifest = json.loads((folder / "octave-frontend.json").read_text(encoding="utf-8"))
        entry = {k: manifest[k] for k in KEYS if k in manifest}
        entry.update(slug=slug, native=True)
        if (folder / "preview.png").is_file():
            (out / slug).mkdir()
            shutil.copy2(folder / "preview.png", out / slug / "preview.png")
            entry["preview"] = f"frontends/{slug}/preview.png"
        native.append(entry)
    (out / "native.json").write_text(json.dumps(native, indent=2) + "\n", encoding="utf-8")
    (out / "catalog.json").write_text(json.dumps(catalog, indent=2) + "\n", encoding="utf-8")
    print(f"staged {len(catalog)} frontends in {out}: {', '.join(e['slug'] for e in catalog)}")


if __name__ == "__main__":
    main()
