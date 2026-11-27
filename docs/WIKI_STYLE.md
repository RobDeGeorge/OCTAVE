# Wiki style

How every page in `wiki/` is written. The model page is `wiki/obd-manager.html`: read it before writing or
rewriting another.

The wiki is one set of pages, one page per topic, for anyone: a driver installing OCTAVE, someone building a
dashboard or a frontend, someone changing the C++. A page serves all three by its **order**, not by being split.

## Order of a page: plain to deep

1. **What it is.** Two or three short paragraphs a newcomer understands: what this part of OCTAVE does for them.
   End with "This page starts with how to use it and gets more technical as it goes" where the page has both.
2. **How to use it.** Steps as the user sees them: the names on the screen, where to tap, what each state means
   and what to do about it. Per-platform differences in one table.
3. **Settings** that belong to the topic, in one table (name on screen, key, default, what it does), with a link
   to the Settings Reference for the rest.
4. **How it works.** Open with one sentence saying the page is technical from here on. Explain the idea before
   the parts: what problem this solves, then the pieces and what each does, then the flow as numbered steps.
5. **Details and special cases**: platforms, limits, edge cases.
6. **When something goes wrong**: where to look, in order.
7. **Files, API and tests**: a short file table, the handful of calls most code needs, the test names. Link to
   `api-reference.html#<object>` for the full list.
8. **Why it is built this way**: a few bullets, each a decision and its reason.
9. Related pages.

Leave out a step that has nothing to say. A page with no user-facing side (Architecture, Utilities) starts at
"what it is" and goes to "how it works".

## Writing

- Short sentences, plain words, active verbs. Explain a term the first time it appears (PID, ELM327, freeze
  frame, context property). Say what a thing is for before how it does it.
- Name things as the reader meets them: the label on the screen, the property or method they would call. Leave
  out private members, timer names and internal function names unless the reader would call or grep for them.
- A table is for things you look up. A numbered list is for steps in order. Everything else is sentences.
- One fact, one place. If another page owns it, link to it; do not repeat it.
- State what something does not do yet in one plain line ("Not on Android yet").
- Numbers the reader can act on stay (30 seconds, 60 lines, 500 MB). Numbers only the code cares about go.

## What does not belong

- **History.** No commit hashes, no dates, no "before X this was broken", no "added in". The page describes the
  app as it is. Git has the rest. A lesson that still guides the code stays, as a rule with its reason.
- **Copies of the generated API.** No tables of every signal, slot and property: `api-reference.html` is built
  from the code. Keep only the short "to do this, use that" table.
- **Code listings that restate the source.** A snippet earns its place only if the reader would write it (a
  QML binding, a JSON entry, a command).
- **Anything you could not confirm in the code.** Check every claim you keep against `src/` and `frontend/`.
  Where the page and the code disagree, the code wins. Where the page contradicts itself, find out which.

## Mechanics

- Keep the page's HTML shell, the classes in `wiki/style.css`, and the table of contents in step with the
  headings.
- Keep every `id` that anything links to. Find them with
  `grep -rhoE '<page>\.html#[a-z0-9-]+' wiki frontend docs src README.md | sort -u`; an id may move to a new
  heading or element but must not vanish. Links out to other pages' anchors must exist there.
- Aim for a third to a half of an old page's length. Length follows from the topic, not from a quota.
- After editing: `venv/bin/python wiki/build_search_index.py`.

## Keeping it this way

When a change adds a fact, put it where the order above says it goes, in a sentence a newcomer can read. Do not
append it to the nearest paragraph. If a section has grown into an inventory, rewrite the section.
