#!/usr/bin/env python3
"""Fail when the published service API (docs/api/) breaks without saying so.

Compares docs/api/ in the working tree with docs/api/ at a git ref (by
default the merge base with origin/main) and sorts every difference into
breaking and additive. Breaking changes to stable members are allowed only
when the protocol version went up and the new version's section in
docs/api/CHANGELOG.md names each one as `object.member`. Members marked
experimental at the base are not covered. See docs/api/README.md, Versioning.

    python tools/api/check_compat.py                 # against the merge base with origin/main
    python tools/api/check_compat.py --base v0.9.5   # against a release
    python tools/api/check_compat.py --draft         # also print a changelog draft

Exit status: 0 compatible (or every break bumped and listed), 1 not, 2 usage.
"""
import argparse
import json
import os
import re
import subprocess
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
API = "docs/api"


def git(*args):
    return subprocess.run(["git", "-C", ROOT, *args], capture_output=True, text=True)


def load_tree(ref):
    """{file name: parsed json} of docs/api at a ref, or the working tree when ref is None."""
    out = {}
    if ref is None:
        for name in os.listdir(os.path.join(ROOT, API)):
            if name.endswith(".json"):
                with open(os.path.join(ROOT, API, name), encoding="utf-8") as fh:
                    out[name] = json.load(fh)
        return out
    listing = git("ls-tree", "--name-only", f"{ref}:{API}")
    if listing.returncode != 0:
        return None
    for name in listing.stdout.split():
        if name.endswith(".json"):
            shown = git("show", f"{ref}:{API}/{name}")
            out[name] = json.loads(shown.stdout)
    return out


def objects(tree):
    """{object name: description} for every service object file in a tree."""
    names = tree.get("index.json", {}).get("objects", {})
    return {n: tree[n + ".json"] for n in names if n + ".json" in tree}


def experimental(member, obj):
    return obj.get("stability") == "experimental" or member.get("stability") == "experimental"


def sig(params):
    return ", ".join(p["type"] for p in params)


def compare_object(name, old, new, breaks, adds):
    if new.get("stability") == "experimental" and old.get("stability") != "experimental":
        breaks.append((name, f"`{name}` became experimental"))

    def note_add(member, what):
        adds.append((f"{name}.{member}", what))

    old_props = {p["name"]: p for p in old.get("properties", [])}
    new_props = {p["name"]: p for p in new.get("properties", [])}
    for pname, op in old_props.items():
        key = f"{name}.{pname}"
        if experimental(op, old):
            continue
        np_ = new_props.get(pname)
        if np_ is None:
            breaks.append((key, f"property `{key}` removed"))
            continue
        if np_["type"] != op["type"]:
            breaks.append((key, f"property `{key}` type {op['type']} -> {np_['type']}"))
        if op.get("readable") and not np_.get("readable"):
            breaks.append((key, f"property `{key}` no longer readable"))
        if op.get("writable") and not np_.get("writable"):
            breaks.append((key, f"property `{key}` no longer writable"))
        if op.get("notify") and not np_.get("notify"):
            breaks.append((key, f"property `{key}` no longer notifies"))
        lost = set(op.get("enum", [])) - set(np_.get("enum", []))
        if lost:
            breaks.append((key, f"property `{key}` lost enum keys {sorted(lost)}"))
        if np_.get("stability") == "experimental" and op.get("stability") != "experimental":
            breaks.append((key, f"property `{key}` became experimental"))
    for pname in new_props.keys() - old_props.keys():
        note_add(pname, "property")

    # `call` picks a method by name and argument count, so that is its identity
    def by_arity(methods):
        return {(m["name"], len(m["params"])): m for m in methods}

    old_m, new_m = by_arity(old.get("methods", [])), by_arity(new.get("methods", []))
    for (mname, arity), om in old_m.items():
        key = f"{name}.{mname}"
        if experimental(om, old):
            continue
        nm = new_m.get((mname, arity))
        if nm is None:
            breaks.append((key, f"method `{key}({sig(om['params'])})` removed"))
            continue
        if sig(nm["params"]) != sig(om["params"]):
            breaks.append((key, f"method `{key}` arguments ({sig(om['params'])}) -> ({sig(nm['params'])})"))
        # A method that returned nothing may start returning something
        if om.get("returns", "") and nm.get("returns", "") != om.get("returns", ""):
            breaks.append((key, f"method `{key}` returns {om.get('returns') or 'nothing'} -> "
                                f"{nm.get('returns') or 'nothing'}"))
        if nm.get("stability") == "experimental" and om.get("stability") != "experimental":
            breaks.append((key, f"method `{key}` became experimental"))
    for mname, arity in new_m.keys() - old_m.keys():
        note_add(mname, f"method ({arity} arguments)")

    # Signal events carry their arguments as a positional array: appending
    # one is additive, anything else moves or retypes what clients read
    old_s = {s["name"]: s for s in old.get("signals", [])}
    new_s = {s["name"]: s for s in new.get("signals", [])}
    for sname, os_ in old_s.items():
        key = f"{name}.{sname}"
        if experimental(os_, old):
            continue
        ns = new_s.get(sname)
        if ns is None:
            breaks.append((key, f"signal `{key}` removed"))
            continue
        ot = [p["type"] for p in os_["params"]]
        nt = [p["type"] for p in ns["params"]]
        if nt[: len(ot)] != ot:
            breaks.append((key, f"signal `{key}` arguments ({', '.join(ot)}) -> ({', '.join(nt)})"))
        elif len(nt) > len(ot):
            note_add(sname, "signal arguments appended")
    for sname in new_s.keys() - old_s.keys():
        note_add(sname, "signal")

    for ename, keys in old.get("enums", {}).items():
        lost = set(keys) - set(new.get("enums", {}).get(ename, []))
        if lost:
            key = f"{name}.{ename}"
            breaks.append((key, f"enum `{key}` lost keys {sorted(lost)}"))


def compare_parameters(old, new, breaks, adds):
    old_p = {p["id"]: p for p in old}
    new_p = {p["id"]: p for p in new}
    for pid, op in old_p.items():
        key = f"parameters.{pid}"
        np_ = new_p.get(pid)
        if np_ is None:
            breaks.append((key, f"parameter `{pid}` removed"))
        elif np_.get("unit") != op.get("unit"):
            breaks.append((key, f"parameter `{pid}` unit {op.get('unit')!r} -> {np_.get('unit')!r}"))
    for pid in new_p.keys() - old_p.keys():
        adds.append((f"parameters.{pid}", "parameter"))


def compare(old_tree, new_tree):
    breaks, adds = [], []
    old_o, new_o = objects(old_tree), objects(new_tree)
    for name, old in old_o.items():
        if name not in new_o:
            if old.get("stability") != "experimental":
                breaks.append((name, f"object `{name}` removed"))
            continue
        compare_object(name, old, new_o[name], breaks, adds)
    for name in new_o.keys() - old_o.keys():
        adds.append((name, "object"))
    if "parameters.json" in old_tree and "parameters.json" in new_tree:
        compare_parameters(old_tree["parameters.json"], new_tree["parameters.json"], breaks, adds)
    return breaks, sorted(adds)


def changelog_section(version):
    """The text under `## Protocol <version>` in docs/api/CHANGELOG.md, or None."""
    path = os.path.join(ROOT, API, "CHANGELOG.md")
    if not os.path.exists(path):
        return None
    with open(path, encoding="utf-8") as fh:
        text = fh.read()
    m = re.search(rf"^## Protocol {version}\b.*?$(.*?)(?=^## |\Z)", text, re.M | re.S)
    return m.group(1) if m else None


def default_base():
    for upstream in ("origin/main", "main"):
        r = git("merge-base", "HEAD", upstream)
        if r.returncode == 0:
            return r.stdout.strip()
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--base", help="git ref to compare against (default: merge base with origin/main)")
    ap.add_argument("--draft", action="store_true", help="print a changelog draft of every change")
    args = ap.parse_args()

    base = args.base or default_base()
    if not base:
        print("check_compat: no base ref (no origin/main or main); pass --base", file=sys.stderr)
        return 2
    old_tree = load_tree(base)
    if old_tree is None:
        print(f"check_compat: {base} has no {API}/, nothing to compare")
        return 0
    new_tree = load_tree(None)

    old_v = old_tree.get("index.json", {}).get("protocolVersion")
    new_v = new_tree.get("index.json", {}).get("protocolVersion")
    breaks, adds = compare(old_tree, new_tree)

    label = git("rev-parse", "--short", base).stdout.strip() or base
    print(f"API at {label}: protocol {old_v}; working tree: protocol {new_v}")
    print(f"  {len(breaks)} breaking, {len(adds)} additive")

    failed = False
    if new_v is not None and old_v is not None and new_v < old_v:
        print(f"FAIL: protocolVersion went down ({old_v} -> {new_v})")
        failed = True
    if breaks:
        if new_v == old_v:
            print(f"FAIL: breaking changes without a protocol version bump (kProtocolVersion is {new_v}):")
            for _, why in breaks:
                print(f"  - {why}")
            print("Keep the old member (mark it deprecated), or bump kProtocolVersion in src/service/protocol.h "
                  "and list each break in docs/api/CHANGELOG.md.")
            failed = True
        else:
            section = changelog_section(new_v)
            if section is None:
                print(f"FAIL: protocol {new_v} has no `## Protocol {new_v}` section in docs/api/CHANGELOG.md")
                failed = True
            missing = [(k, why) for k, why in breaks if section is not None and f"`{k}`" not in section]
            if missing:
                print(f"FAIL: breaking changes not listed under `## Protocol {new_v}` in docs/api/CHANGELOG.md:")
                for _, why in missing:
                    print(f"  - {why}")
                failed = True
            if section is not None and not missing:
                print(f"ok: protocol {old_v} -> {new_v}, every break is in the changelog")
    if new_v != old_v and new_v is not None and changelog_section(new_v) is None and not failed:
        print(f"FAIL: protocol {new_v} has no `## Protocol {new_v}` section in docs/api/CHANGELOG.md")
        failed = True

    if args.draft:
        print("\nChangelog draft:")
        for _, why in breaks:
            print(f"- Breaking: {why}")
        for key, what in adds:
            print(f"- Added {what}: `{key}`")
    if not failed:
        print("ok: compatible" if not breaks else "ok")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
