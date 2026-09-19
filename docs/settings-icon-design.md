# Settings icon design

The settings symbols use simple, front-facing shapes with rounded ends and open interiors. The button pairs each symbol closely with its label so the two read as one target.

## References

- [Apple: Icons](https://developer.apple.com/design/human-interface-guidelines/icons) — recognizable metaphors, consistent visual weight, optical centering, and matching icon and text emphasis.
- [Apple: SF Symbols](https://developer.apple.com/sf-symbols/) — coordinated symbol families and restrained hierarchical rendering with a single tint.
- [Phosphor](https://github.com/phosphor-icons/core) — reference for a family organized by weight, including layered duotone treatments.

The SVG paths are drawn locally; the app does not load an external icon library or Apple font.

## Drawing rules

- Use a 32 × 32 viewBox, normally keeping artwork inside a 4-unit margin. Narrow or solid symbols need optical adjustments rather than identical bounding boxes.
- Default to a 1.8-unit stroke with rounded caps and joins. Sparse waveform strokes use 2.2 units to match the visual weight of enclosed icons.
- Author in white so `ThemedIcon` can apply the active theme's accent. Use 12–18% fill opacity only for a secondary surface; the outline carries recognition.
- Keep a single clear metaphor: music pages for Library, a waveform for Now Playing, a chain for Connection, and faders for Tuning.
- Avoid decorative tick marks, screws, lettering, and perspective. Check small renders before adding detail.
- Category artwork uses `cat_*.svg`; subsection artwork uses `tile_*.svg`. Keep shared main-navigation assets separate.

## Button presentation

`SettingsTile.qml` centers the icon and label as a group, with a 12 dp gap and an icon box up to 64 dp. Smaller tiles reduce the icon box to leave room for the label. Labels use medium weight and retain the app's text scaling. The icon has no offset shadow and the tile has no decorative bevel.

Review SVGs at 24 and 64 px, and render the actual `SettingsTile` with `ThemedIcon` on both light and dark themes: a raw SVG sheet does not verify the Qt tinting path.
