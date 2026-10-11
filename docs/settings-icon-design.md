# Settings icon design

This covers the category icons (`cat_*.svg`). Settings tiles have no icons: each shows the setting's current state as text (see "Settings tiles" below).

The category symbols use simple, front-facing shapes with rounded ends and open interiors. The button pairs each symbol closely with its label so the two read as one target.

## References

- [Apple: Icons](https://developer.apple.com/design/human-interface-guidelines/icons) — recognizable metaphors, consistent visual weight, optical centering, and matching icon and text emphasis.
- [Apple: SF Symbols](https://developer.apple.com/sf-symbols/) — coordinated symbol families and restrained hierarchical rendering with a single tint.
- [Phosphor](https://github.com/phosphor-icons/core) — reference for a family organized by weight, including layered duotone treatments.

The SVG paths are drawn locally; the app does not load an external icon library or Apple font.

## Drawing rules

- Use a 32 × 32 viewBox, normally keeping artwork inside a 4-unit margin. Narrow or solid symbols need optical adjustments rather than identical bounding boxes.
- Default to a 1.8-unit stroke with rounded caps and joins. Sparse waveform strokes use 2.2 units to match the visual weight of enclosed icons.
- Author in white so `ThemedIcon` can apply the active theme's accent. Use 12–18% fill opacity only for a secondary surface; the outline carries recognition.
- Keep a single clear metaphor, specific to what the section holds (an OBD port for OBD, a monitor for Display) rather than a generic stand-in.
- Avoid decorative tick marks, screws, lettering, and perspective. Check small renders before adding detail.
- Category artwork uses `cat_*.svg`. Keep shared main-navigation assets separate.

## Settings tiles

`SettingsTile.qml` follows the Android Quick Settings / Apple Home pattern: the setting's name is the largest text, with one short line of current state under it ("Connected", "7:57 AM", "4 gauges") and a status dot for anything that connects. A tile whose thing is on or connected is tinted with the accent; display options stay neutral. A tile with no state shows its name alone. States come from each `tileModel` entry's `summary()` function; keep them to about 14 characters and never fill the line with an instruction ("Tap to …"). Text sizes follow the tile width, not the text, so tiles in a grid match.

Review category SVGs at 24 and 64 px, and render them through `ThemedIcon` on both light and dark themes: a raw SVG sheet does not verify the Qt tinting path.
