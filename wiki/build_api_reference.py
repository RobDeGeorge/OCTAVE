#!/usr/bin/env python3
"""Build wiki/api-reference.html from docs/api/ (the published service API).

Run after `./build/octave --dump-api docs/api`, then `python wiki/build_search_index.py`.
The page is generated: edit the C++ (or docs/api/README.md), not the HTML.
"""
import html
import json
import os

WIKI = os.path.dirname(os.path.abspath(__file__))
API = os.path.join(WIKI, "..", "docs", "api")
OUT = os.path.join(WIKI, "api-reference.html")

# The wiki page (and heading) that explains what each object is for. This page only lists
# members; add a row when a new object is exposed.
GUIDE = {
    "airPlayManager": ("phone-mirror-android-auto.html#airplay", "Phone Mirror & Android Auto: AirPlay"),
    "androidAutoManager": ("phone-mirror-android-auto.html#android-auto", "Phone Mirror & Android Auto: Android Auto"),
    "audioAnalyzer": ("audio-analyzer.html", "Audio Analyzer"),
    "berryIMU": ("hardware-managers.html#imu-overview", "Hardware Managers: the motion sensor"),
    "busMonitor": ("obd-manager.html#bus-monitor", "OBD Manager: Bus Monitor"),
    "clock": ("utilities.html#clock", "Utilities: Clock"),
    "customPids": ("obd-manager.html#custom-pids", "OBD Manager: Custom PIDs"),
    "dashboardManager": ("gauges-dashboards.html", "Gauges & Dashboards"),
    "diagnosticsManager": ("utilities.html#diagnostics-manager", "Utilities: Diagnostics"),
    "esp32VolumeManager": ("hardware-managers.html#esp32-overview", "Hardware Managers: the volume knob"),
    "fontCatalog": ("theme-system.html#font-system", "Theme System: Fonts"),
    "frontendManager": ("writing-a-frontend.html#install", "Writing a Frontend: Installing"),
    "gestureSensor": ("hardware-managers.html#gesture-overview", "Hardware Managers: the gesture sensor"),
    "mediaManager": ("media-manager.html", "Media Manager"),
    "networkManager": ("network-manager.html", "Network Manager"),
    "nowPlaying": ("media-manager.html#now-playing", "Media Manager: Now Playing"),
    "obdAdapterFinder": ("obd-manager.html#adapter-finder", "OBD Manager: finding an adapter"),
    "obdManager": ("obd-manager.html", "OBD Manager"),
    "paramStore": ("obd-manager.html#supported-parameters", "OBD Manager: readings"),
    "parameterCatalog": ("obd-manager.html#supported-parameters", "OBD Manager: readings"),
    "phoneMirrorManager": ("phone-mirror-android-auto.html#phone-mirror", "Phone Mirror & Android Auto: Phone Mirror"),
    "settingsManager": ("settings-manager.html", "Settings Manager"),
    "shiftLightManager": ("obd-manager.html#shift-light", "OBD Manager: Shift Light"),
    "sim": ("development.html#mock-obd", "Development: the simulated car"),
    "spotifyManager": ("media-manager.html#spotify", "Media Manager: Spotify"),
    "themeCatalog": ("theme-system.html", "Theme System"),
    "tripRecorder": ("obd-manager.html#black-box", "OBD Manager: the black box"),
    "units": ("utilities.html#platform", "Utilities: small helpers"),
    "vehicleManager": ("pages.html#carmenu", "Pages: 3D View"),
    "volumeController": ("utilities.html#volume-controller", "Utilities: Volume"),
}


def guide_link(name):
    """'Explained in: <link>. ' for an object, or '' (with a warning) when GUIDE has no usable row."""
    if name not in GUIDE:
        print(f"warning: no GUIDE row for {name}; its section links to no wiki page")
        return ""
    href, label = GUIDE[name]
    page, _, anchor = href.partition("#")
    path = os.path.join(WIKI, page)
    if not os.path.exists(path):
        raise SystemExit(f"GUIDE[{name}]: wiki/{page} does not exist")
    if anchor:
        with open(path, encoding="utf-8") as fh:
            if f'id="{anchor}"' not in fh.read():
                raise SystemExit(f"GUIDE[{name}]: wiki/{page} has no id \"{anchor}\"")
    return f'Explained in: <a href="{esc(href)}">{esc(label)}</a>. '


def load(name):
    with open(os.path.join(API, name), encoding="utf-8") as fh:
        return json.load(fh)


def esc(text):
    return html.escape(str(text), quote=True)


def code(text):
    return f"<code>{esc(text)}</code>" if text else "&ndash;"


def signature(method):
    params = ", ".join(f"{p['type']} {p['name']}".strip() for p in method["params"])
    return f"{method['name']}({params})"


def access(prop):
    if prop["constant"]:
        return "constant"
    return f"read / write ({esc(prop['scope'])})" if prop["writable"] and prop.get("scope") else (
        "read / write" if prop["writable"] else "read")


def marks(member):
    """Experimental / deprecated notes after a member (docs/api/README.md, Stability)."""
    out = ""
    if member.get("stability") == "experimental":
        out += ' <span class="cmt">(experimental)</span>'
    if member.get("deprecated"):
        out += f' <span class="cmt">(deprecated: {esc(member["deprecated"])})</span>'
    return out


def object_section(name, obj):
    out = [f'        <h3 id="{esc(name)}"><code>{esc(name)}</code> <span class="cmt">&mdash; {esc(obj["class"])}</span></h3>']
    if obj.get("stability") == "experimental":
        out.append("        <p><strong>Experimental:</strong> this object may change or go in any release.</p>")
    out.append(f'        <p>{guide_link(name)}Raw: <span class="file-path">docs/api/{esc(name)}.json</span>.</p>')
    props = obj["properties"]
    if props:
        out.append("        <table>\n          <thead><tr><th>Property</th><th>Type</th><th>Access</th><th>Notify</th></tr></thead>\n          <tbody>")
        for p in props:
            kind = esc(p["type"])
            if p.get("enum"):
                kind += " (" + " | ".join(esc(k) for k in p["enum"]) + ")"
            out.append(f"            <tr><td>{code(p['name'])}{marks(p)}</td><td>{kind}</td><td>{access(p)}</td><td>{code(p.get('notify', ''))}</td></tr>")
        out.append("          </tbody>\n        </table>")
    methods = obj["methods"]
    if methods:
        out.append("        <table>\n          <thead><tr><th>Method (<code>call</code>)</th><th>Returns</th><th>Kind</th><th>Needs</th></tr></thead>\n          <tbody>")
        for m in methods:
            out.append(f"            <tr><td>{code(signature(m))}{marks(m)}</td><td>{code(m['returns'])}</td><td>{esc(m['kind'])}</td><td>{code(m.get('scope', ''))}</td></tr>")
        out.append("          </tbody>\n        </table>")
    sigs = obj["signals"]
    if sigs:
        out.append("        <p><strong>Signals</strong> (<code>subscribe</code>): " + ", ".join(code(signature(s)) + marks(s) for s in sigs) + ".</p>")
    for enum, keys in sorted(obj.get("enums", {}).items()):
        out.append(f"        <p><strong>Enum</strong> <code>{esc(enum)}</code>: " + ", ".join(code(k) for k in keys) + " (by name or number).</p>")
    return "\n".join(out)


def parameter_table(params):
    rows = []
    for p in params:
        alias = f" (alias of {code(p['aliasOf'])})" if p.get("aliasOf") else ""
        rows.append(f"            <tr><td>{code(p['id'])}{alias}</td><td>{esc(p['title'])}</td><td>{esc(p['unit']) or '&ndash;'}</td>"
                    f"<td>{esc(p['min'])} &ndash; {esc(p['max'])}</td><td>{esc(p['kind'])}</td><td>{esc(p['source'])}</td></tr>")
    return ("        <table>\n          <thead><tr><th>Id</th><th>Title</th><th>Native unit</th><th>Range</th><th>Kind</th><th>Source</th></tr></thead>\n"
            "          <tbody>\n" + "\n".join(rows) + "\n          </tbody>\n        </table>")


def main():
    index = load("index.json")
    names = list(index["objects"])
    objects = {n: load(f"{n}.json") for n in names}
    params = load("parameters.json")
    toc = "\n".join(f'            <li><a href="#{esc(n)}">{esc(n)}</a></li>' for n in names)
    sections = "\n\n".join(object_section(n, objects[n]) for n in names)
    page = f"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Service API Reference - OCTAVE Wiki</title>
  <link rel="stylesheet" href="style.css">
</head>
<body>
  <div class="wiki-layout">
    <div class="content">
      <div class="content-inner">
        <!-- Generated by wiki/build_api_reference.py from docs/api/; do not edit -->
        <div class="page-header">
          <div class="breadcrumb"><a href="index.html">OCTAVE Wiki</a> / Service API Reference</div>
          <h1 class="page-title">Service API Reference</h1>
          <p class="page-subtitle">Every object a frontend can read and drive over the service protocol, generated from the C++ meta-objects (protocol version {index['protocolVersion']}).</p>
        </div>

        <nav class="toc">
          <h3>Table of Contents</h3>
          <ul class="toc-list">
            <li><a href="#overview">Overview</a></li>
            <li><a href="#objects">Objects</a></li>
            <li><a href="#parameters">Parameters</a></li>
          </ul>
        </nav>

        <h2 id="overview">Overview</h2>
        <p>This page is generated from <span class="file-path">docs/api/</span> (<code>./build/octave --dump-api docs/api</code>, then <code>python wiki/build_api_reference.py</code>); <code>tst_apidocs</code> fails when that copy is out of date. The protocol itself (<code>hello</code>, <code>describe</code>, <code>get</code>, <code>set</code>, <code>call</code>, <code>subscribe</code>, <code>rows</code>, <code>stream</code>, <code>pair</code>) is specified in <span class="file-path">docs/api/protocol.json</span> (JSON Schema, checked against the server) and described in <a href="utilities.html#service-protocol">Utilities: Service protocol</a>; versioning and the dashboard file schema in <span class="file-path">docs/api/README.md</span> and <span class="file-path">docs/api/dashboard-spec.schema.json</span>. This page lists members and their types. What an object is for, and the calls most code needs, are on the page each section links to.</p>
        <p>The same objects are QML context properties under the same names. Types are Qt's names (<code>QString</code>, <code>QVariantMap</code>, ...); on the wire they are JSON. {len(names)} objects:</p>
        <ul>
{toc}
        </ul>

        <h2 id="objects">Objects</h2>
{sections}

        <h2 id="parameters">Parameters</h2>
        <p>Every OBD PID and IMU reading ({len(params)}), the keys of <code>paramStore</code> (<span class="file-path">docs/api/parameters.json</span>). Values are in the native unit; <code>parameterCatalog.toDisplay(id, value)</code> converts to the user's units.</p>
{parameter_table(params)}

        <div class="related-pages">
          <h3>Related Pages</h3>
          <a href="utilities.html#service-protocol" class="related-link">Service protocol</a>
          <a href="writing-a-frontend.html" class="related-link">Writing a Frontend</a>
          <a href="architecture.html" class="related-link">Architecture Overview</a>
        </div>
      </div>
    </div>
  </div>
  <script src="search-index.js"></script>
  <script src="search.js"></script>
  <script src="nav.js"></script>
</body>
</html>
"""
    with open(OUT, "w", encoding="utf-8") as fh:
        fh.write(page)
    print(f"Wrote {OUT}: {len(names)} objects, {len(params)} parameters")


if __name__ == "__main__":
    main()
