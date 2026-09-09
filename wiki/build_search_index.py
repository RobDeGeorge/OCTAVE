#!/usr/bin/env python3
"""Rebuild search-index.js from all wiki HTML pages. Run after adding/editing pages."""
import re, html, json, glob, os

# Work from the wiki directory regardless of where this is invoked. The globs
# below are relative, so running it from the repo root used to silently emit an
# empty index into the repo root instead of updating wiki/search-index.js.
os.chdir(os.path.dirname(os.path.abspath(__file__)))

pages = []
for f in sorted(glob.glob("*.html")):
    with open(f) as fh:
        content = fh.read()

    m = re.search(r'class="page-title">(.*?)</h1>', content)
    title = m.group(1) if m else f.replace(".html", "").replace("-", " ").title()

    m = re.search(r'class="page-subtitle">\s*(.*?)\s*</p>', content, re.DOTALL)
    subtitle = html.unescape(re.sub(r"<[^>]+>", "", m.group(1).strip())) if m else ""

    headings = []
    for m in re.finditer(r'<h([23])[^>]*id="([^"]*)">(.+?)</h\1>', content, re.DOTALL):
        hid, htxt = m.group(2), re.sub(r"<[^>]+>", "", m.group(3)).strip()
        headings.append({"id": hid, "text": htxt, "level": int(m.group(1))})

    body = re.sub(r'<nav class="toc">.*?</nav>', "", content, flags=re.DOTALL)
    body = re.sub(r"<script[^>]*>.*?</script>", "", body, flags=re.DOTALL)
    body = re.sub(r"<[^>]+>", " ", body)
    body = re.sub(r"\s+", " ", html.unescape(body)).strip()[:5000]

    pages.append({"url": f, "title": title, "subtitle": subtitle, "headings": headings, "body": body})

with open("search-index.js", "w") as out:
    out.write("// Auto-generated search index \u2014 regenerate with: python3 build_search_index.py\n")
    out.write("var SEARCH_INDEX = ")
    json.dump(pages, out)
    out.write(";\n")

print(f"Generated search-index.js with {len(pages)} pages")
