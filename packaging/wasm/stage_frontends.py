#!/usr/bin/env python3
"""Stage the web frontends the browser build offers, and their catalog.

    python3 packaging/wasm/stage_frontends.py OUT_DIR

Copies each built web frontend below into OUT_DIR/frontends/<slug>/ and
writes OUT_DIR/frontends/catalog.json: one entry per staged frontend, made
from its own octave-frontend.json (id, name, version, description, protocol,
scopes) plus `slug` and `path` (its folder, relative to the site). The
browser build lists them in Settings > Device > Frontends (index.html,
src/platform/wasm/webfrontends.h) and the playground in its picker
(playground.html); `?frontend=<slug>` picks one on either page.

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
        catalog.append(entry)
    (out / "catalog.json").write_text(json.dumps(catalog, indent=2) + "\n", encoding="utf-8")
    print(f"staged {len(catalog)} frontends in {out}: {', '.join(e['slug'] for e in catalog)}")


if __name__ == "__main__":
    main()
