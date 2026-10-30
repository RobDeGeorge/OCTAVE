# Touch-first widget / layout editors: research study for the OCTAVE dashboard editor

Date: 2026-10-30. Scope: non-automotive layout editors with very large user bases, and the published guidance on direct manipulation by touch. Every claim has a source URL.

Markers:
- **[V]** = verified on a primary page I fetched (vendor docs, release notes, the HIG, NN/g).
- **[S]** = secondary source (press, guides, forums), fetched or seen in search snippets.
- **[U]** = unverified (my product knowledge, or one weak source). Check before relying on it.

Context for the recommendations: OCTAVE's editor (`frontend/dashboards/editor/`) already does tap-to-place from a palette, drag-to-move with grid snap, one bottom-right corner grip on the selected widget, steppers in the properties panel, and a "no overlap" rule that snaps a rejected drag back (`EditorCanvas.qml` / `DashboardEditor.qml` comments).

---

## 0. Published guidance on direct manipulation by touch

### Target sizes
- Apple HIG (Buttons): "a button needs a hit region of at least 44x44 pt — in visionOS, 60x60 pt". Also "Always include a press state for a custom button." [V] https://developer.apple.com/design/human-interface-guidelines/buttons.md
- Material: touch targets at least **48x48 dp with at least 8 dp between them**. The visible icon can be 24 dp inside a 48 dp hit area. [S] https://material.io/design/layout/spacing-methods.html , https://support.google.com/accessibility/android/answer/7101858
- NN/g: "Ensure that all interactive elements are at least 1cm × 1cm (0.4in × 0.4in) in physical, rendered size". Targets must first be big enough and then spaced well enough. "if an app ... is to be used when the user is moving, targets will be harder to hit", so make them bigger. [V] https://www.nngroup.com/articles/touch-target-size/
- WCAG 2.2: 24x24 CSS px is the AA minimum (2.5.8) and 44x44 the AAA one (2.5.5). [S] https://docs.deque.com/devtools-mobile/2025.7.2/en/ios-touch-target-spacing
- Hoober (UXmatters 2013): accuracy depends on where on the screen the target is. In the centre, targets can be about 7 mm apart; edges and corners need more room. I could not confirm the commonly quoted 11-12 mm corner figure. [S] https://uxmatters.com/mt/archives/2013/11/design-for-fingers-and-thumbs-instead-of-touch.php
- **In-vehicle:** older recommendations put the minimum touchscreen button at **19 mm** (Monterey Technologies 1996) or **22 mm** (Greenstein 1997), about fingertip size (16-20 mm). Feng et al. 2018 tested 14, 24 and 33 mm buttons for their effect on drivers' glances. NHTSA's 2013 guidelines limit glance time (no glance over 2 s, 12 s total per task) and give no millimetre size. [S] https://public.websites.umich.edu/~fredfeng/files/IJHCI_Feng2018_AcceptedManuscript.pdf , https://www.govinfo.gov/link/fr/78/24861
- Apple widget HIG: text at 11 pt or larger, standard widget margin 16 pt (11 pt for tight groupings). "make sure people can tap or click them with confidence and without accidentally performing unintended interactions." In CarPlay, "Glanceable information and large text are especially important". [V] https://developer.apple.com/design/human-interface-guidelines/widgets.md

### Drag thresholds, long-press timing, feedback
- Apple HIG (Drag and drop): "**Display a drag image as soon as people drag a selection about three points.**" A translucent drag image works well. Show whether a destination can accept the item, and highlight it only while the item is over it. On an invalid drop, animate the item back to its source. "**Prefer letting people undo a drag-and-drop operation.**" "**Offer alternative ways to accomplish drag-and-drop actions**" (menus, accessibility). Auto-scroll the destination when dragging inside a scrolling container. After a drop, the item stays selected: "People expect the content they drop to remain selected so they can immediately act on it." [V] https://developer.apple.com/design/human-interface-guidelines/drag-and-drop.md
- NN/g (Drag-and-drop): show the grabbed state (shadow, offset or tilt, translucent ghost). Show the drop zone before release. Magnetism, meaning a drop zone that reaches past its visible border, reduces errors. "It's important to show the background objects moving out of the way before the user releases the item", in an animation of about 100 ms with easing. Trigger the reshuffle when the **dragged item's centre crosses the other item's edge**. On touch, use a short delay to tell tap, swipe and grab apart, leave at least 1 cm x 1 cm of free space to drag in, and use haptics on grab and on drop-zone hover. Pair drag with a precise alternative such as arrows or menus. [V] https://www.nngroup.com/articles/drag-drop/
- Android: base `TOUCH_SLOP = 8` dp (AOSP ViewConfiguration). [S] https://code.yawk.at/android/android-9.0.0_r35/android/view/ViewConfiguration.java (search snippet). AOSP moved `DEFAULT_LONG_PRESS_TIMEOUT` from 500 to 400 ms. The Pixel "touch & hold delay" setting offers 400 ms (default) / 1000 / 1500 ms. [S] https://gitlab.e.foundation/e/os/android_frameworks_base/-/commit/028995a590e3ef5a85d75e6745aaf0a7e78d771f , https://helentech.jp/how-to-pixel-touch-hold-delay-86920/ . Flutter and React Aria still default to 500 ms. [S] https://api.flutter.dev/flutter/gestures/kLongPressTimeout-constant.html
- Meta touch guidance: a press delay of about 0.5 s before long-press activates, release before that counts as a tap, and long press is less discoverable, so it should never be the only way to reach a critical action. SAP Fiori: start a drag after a short hold **or** from a dedicated handle, so scrolling and tapping don't drag by accident. [S] https://developers.meta.com/horizon/design/touch_bp/ , https://www.sap.com/design-system/fiori-design-web/v1-136/foundations/interaction/gestures
- Flutter `ReorderableListView`: mobile starts a drag on a long press anywhere on the item, while desktop uses a visible drag handle. [S] https://api.flutter.dev/flutter/material/ReorderableListView/buildDefaultDragHandles.html

### Fat finger / occlusion
- Holz & Baudisch (CHI 2010, 2011): most "fat finger" error is a **systematic offset** that depends on finger posture and on the user. Their model accounted for 67% of the inaccuracy, and the projected-centre model cuts the error to about 1.6 mm, against 4 mm for the traditional model. In practice: a dragged object should follow the finger with a constant offset (the grab point stays where the finger first landed) and never jump so that its centre is under the finger, and drop targets should be generous. [S] https://www.christianholz.net/generalized_perceived_input_point_model , https://www.academia.edu/1186917/Understanding_Touch
- Practical consequence used by every editor below: handles and readouts sit **outside** or at the edge of the content, so the finger doesn't hide what it is changing, and the hit area of a handle is bigger than its drawn glyph. [U, inferred from the HIG and Material hit-area guidance above]

---

## 1. iOS / iPadOS Home Screen widgets, Apple Watch faces, CarPlay

### Entering and leaving edit mode
- Long-press the wallpaper or any icon or widget to open a context menu with "Edit Home Screen", or keep holding to enter "jiggle" (wiggle) mode directly. Tap Done, or press Home / swipe up, to leave. [S] https://macrumors.com/guide/ios-18-home-screen
- Complaints about edit mode starting by accident: "they often start jiggling just when I pick the phone up", and inadvertently moving or removing icons. [S] https://origin-discussions2-us-dr-prz.apple.com/thread/254358795 , https://discussions.apple.com/thread/255427582 (a request to "Lock the iPad icons so they can't accidentally be moved")

### Adding widgets (gallery)
- The "+" button in jiggle mode opens the **widget gallery**: suggested widgets at the top, a search field, then apps. Tap an app to see its widgets as **live previews**, swipe through the sizes (page dots), then tap "Add Widget" or drag the preview onto the page. [S] https://ios.gadgethacks.com/how-to/add-widgets-your-iphones-home-screen-ios-14-0319224/ , https://www.macworld.com/article/3566286/how-to-add-remove-and-customize-widgets-in-ios-14.html
- HIG rules for gallery entries: "**Design a realistic preview to display in the widget gallery**", using realistic simulated data if real data is slow. "**Group your widget's sizes together, and provide a single description**" that begins with an action verb. Placeholder content uses semi-opaque shapes where the data will go. [V] https://developer.apple.com/design/human-interface-guidelines/widgets.md
- HIG on sizes: "Avoid expanding a smaller widget's content to simply fill a larger area." Each size should add a layer of information, not just get bigger. "Balance information density." [V] same URL

### Moving, collisions, reflow
- Items sit on a fixed 4-column grid (6 rows on iPhone). Before iOS 18, everything **packed toward the top-left**. Dropping a widget pushed icons along in reading order and overflowed them onto the next page. iOS 18 lets you leave gaps ("app icon-sized spaces between apps, full empty rows, and full empty columns"), but placement still snaps to the grid. [V-ish, MacRumors] https://macrumors.com/guide/ios-18-home-screen
- This reflow is the most-criticised part of iOS editing:
  - 9to5Mac: "the absolute user-interface disaster that is the process of rearranging your apps on an iPhone"; "It's easy to accidentally drag an app past the screen you want. Or into the wrong position. Or accidentally create a folder."; multi-select is "super-fiddly"; "The whole process is so bad I often put off any significant reorganizing". [V] https://9to5mac.com/2023/10/09/iphone-jiggle-mode-2/
  - Digital Trends: icons "jumping out of the way unpredictably, or not at all", and moving an icon across pages "is often a Sisyphean task". [S] https://www.digitaltrends.com (search snippet; article URL 404'd)
  - iOS 14: adding a large widget made folders disappear. [S] https://developer.apple.com/forums/thread/656888 , https://talk.tidbits.com/t/warning-about-ios-14-home-screen-widgets/13605
- **Stacks:** drag a widget onto another of the same size to make a stack (up to 10), then swipe vertically to flip through them. Smart Stack rotates automatically. [S] https://ios.gadgethacks.com/how-to/add-ios-14s-new-smart-stack-widget-your-iphones-home-screen-0320441/

### Resizing
- **iOS 18:** each widget in jiggle mode gets "a white rounded bar in the corner, which you can drag to make them larger or smaller". It **snaps only between the sizes the widget declares** (S, M, L, XL on iPad); you can't free-size. [V-ish, MacRumors] https://macrumors.com/guide/ios-18-home-screen . The long-press context menu also offers size choices, so you no longer remove and re-add a widget to change its size. [S] https://ios.gadgethacks.com/how-to/ios-18-lets-you-change-widget-sizes-your-iphones-home-screen-without-deleting-them-first-0385553/ , https://tomsguide.com/phones/iphones/how-to-quickly-resize-a-home-screen-widget-in-ios-18 (size-icon row in the menu: [U])

### Deleting, settings, undo
- A "–" badge on the item's corner in jiggle mode, or "Remove Widget" in the context menu, with a confirmation. [S/U]
- Per-widget settings: long-press, then "Edit Widget". The widget **flips over in place** to a short form, usually 1 to 4 fields. The HIG asks you to choose between automatic content and customisation, so most widgets expose very few options. [V for the HIG point] https://developer.apple.com/design/human-interface-guidelines/widgets.md ; flip animation [U]
- There is no undo for Home Screen edits. Shaking to undo doesn't apply. [U]

### Apple Watch face editor (a template-first editor)
- Touch and hold the face, then tap Edit. **Swipe** between pages (style, colour, complications). On the complications page, **tap a slot**, then **turn the Digital Crown** to cycle through what goes in it. Press the Crown to save. [S/V, Apple Support] https://support.apple.com/guide/watch/apd8d0b9c582/watchos , https://support.apple.com/en-mk/guide/watch-ultra/apda6559ad78/watchos
- Lesson: a fixed **template with named slots**, plus a **"select slot, then scrub a list"** picker. There's no free layout at all, and that is why it works on a 40 mm screen. The iPhone Watch app offers the same editing with a larger preview (Face Gallery). [S]

### CarPlay (iOS 26)
- The widgets screen sits left of the dashboard and holds **stacks (columns) of up to 5 widgets**. The car's screen decides how many columns show (1 to 3). Editing happens **on the phone** (Settings > General > CarPlay > car > Widgets), with Smart Rotate on or off per stack. Some widgets have an "i" button for settings. [S] https://www.tomsguide.com/phones/iphones/how-to-use-ios-26-carplay-widget-features-on-your-iphone , https://9to5mac.com/2026/05/15/carplay-in-ios-26-adds-five-new-ways-to-customize-your-setup/
- HIG: CarPlay uses the small widget "with the background removed and scaled up to best fit the grid". [V] https://developer.apple.com/design/human-interface-guidelines/widgets.md
- Lesson: Apple moves layout editing **off the car screen** onto a handheld device, and on the car screen it offers only reordering inside a fixed column template.

---

## 2. Android launchers (Pixel, Nova) and KWGT / KLWP

### Pixel Launcher (AOSP Launcher3)
- **Enter:** long-press empty wallpaper for a menu with "Widgets", "Wallpaper & style" and "Home settings". Long-press a widget to lift it and drag it right away. [S] https://9to5google.com/?p=214999
- **Widget picker (Android 12+):** a bottom sheet with a **search bar**, a **"suggested" row**, and a **collapsible card per app**, only one expanded at a time, with live previews. Long-press a preview and drag it onto the page. [S] https://www.androidpolice.com/2021/04/21/android-12-dp3-adds-a-search-bar-to-its-redesigned-widget-picker/ , https://www.droid-life.com/2021/03/17/whats-new-in-android-12-dp2-the-fun-stuff/ . Newer Pixels also have a tap-to-add "Add" button. [U]
- **Resize:** after you **long-press and release** a placed widget, a frame with **4 edge dots** (a handle on each side) appears. Dragging a dot snaps to cells, within the widget's declared `minResizeWidth/Height` .. `maxResizeWidth/Height`, and the widget can switch to a different layout at size breakpoints (`RemoteViews` size mappings). [V, Android docs] https://developer.android.com/develop/ui/views/appwidgets/layouts ; resize frame [S] https://forum.beeminder.com/t/stretched-out-widgets/6110
- **Collisions:** dropping onto occupied cells **pushes the other items into free cells** ("reorder" animation), or refuses the drop if there's no room (the widget springs back with a "No more room" toast). [U, Launcher3 behaviour from memory]
- **Accidental resize:** in a Quire feedback thread, touching a widget pops the resize frame by accident. [S] https://quire.io/w/Quire_Feedback/1303

### Nova Launcher (power-user favourite)
- **Subgrid positioning:** items snap to **half cells**, so a 5x1 widget can become 5x1.5; "That essentially doubles the options when resizing a widget". It's hidden in Desktop > Desktop grid. A writer calls it the feature that keeps bringing them back. [S] https://www.androidcentral.com/subgrid-positioning-why-i-keep-coming-back-nova-launcher , https://www.phonearena.com/news/Nova-Launcher-updated-to-version-3.0-now-supports-Ok-Google-and-subgrid-positioning_id56846
- **User-chosen grid density** up to 12x12, and 16x16 in a hidden Labs mode. [S] https://forum.beeminder.com/t/fitting-more-widgets-in-an-android-home-screen/5460
- **Optional overlap** with "send to back / bring to front" since Nova 1.2 (2012). It's **off by default**, an expert opt-in. Lawnchair has an "Allow overlap" switch as well. [S] https://www.androidpolice.com/2012/06/08/nova-launcher-updated-with-color-themes-widget-overlapping-and-icon-themes/ , https://forums.androidcentral.com/threads/help-with-nova.519163/
- **Desktop lock:** Nova lets you lock the desktop so a stray long press can't move anything. [U, from product knowledge]

### KWGT / KLWP (Kustom), r/kustom about 94k members
- **Model:** a full design tool on the phone. A preview sits at the top and a tabbed **bottom panel** below it (Items / Background / Layer / Globals / Shortcuts). Items form a **layer tree** (stacks, overlap groups); each item has a Position tab with anchor plus X/Y offsets and **+/- steppers**, and any value can be a **formula**. There's a snap-to-grid toggle in the toolbar. You start from a preset (template) or from an empty canvas. [S] https://www.guidingtech.com/use-kwgt-make-widgets/ ; layer and formula details [U]
- **Praise:**
  - Thumbnails show "a feedforward of how the widget would look on your current home screen background". [S, Pratt IxD critique] https://ixd.prattsi.org/2021/09/design-critique-kwgt-app/
  - Unlimited power, and the large ecosystem of community presets. [S] https://www.makeuseof.com/free-app-turned-android-home-screen-into-personal-dashboard/
- **Complaints:**
  - Unlabelled icons, so you have to tap each one to learn what it does; no in-app instruction for adding a widget; finding an item means drilling into categories; weak search; raw formula code confuses newcomers. [S] https://ixd.prattsi.org/2021/09/design-critique-kwgt-app/
  - "I'm really confused about how to use the KWGT app." [S] https://forum.kustom.rocks/t/i-am-exploring-the-uses-and-benefits-of-kwgt/9280
  - The bottom editor panel is too small: "The dimunitive panel height makes it extremely annoying to use the app and edit widgets." [V] https://forum.kustom.rocks/t/kwgt-pro-bottom-editor-panel-height-too-small-not-resizable/7632 . "so much unused space in the preview window" [S] https://forum.kustom.rocks/t/editors-bottom-panel-and-preset-export-update/13358
  - Positioning by numeric offset rather than direct drag. I found no source saying you can drag items in the preview. [U]
- **Lesson:** KWGT shows the ceiling of a property-panel-first editor on a phone. It's powerful, but people find it hard to learn. The things to copy are its **preset-first start** and **live preview on the real background**, not its numeric-only positioning.

---

## 3. Home Assistant dashboards: from Masonry to Sections (2024)

### Why they replaced Masonry (official, 2024-03-04)
- "Masonry lays out cards in a logic that may not be immediately clear and predictable to many users." Card heights vary, so a 1-pixel difference can move a card to another column. "it is a wall of cards which does not care whether the bricks are laid." Users lose their spatial memory when the same dashboard is opened on another screen, and they work around it. The redesign came after "months of user research and ideation", including user interviews. [V] https://www.home-assistant.io/blog/2024/03/04/dashboard-chapter-1/
- The docs explain the mechanism: each card reports a size (1 unit is about 50 px) and goes into the shortest column. [S] https://www.home-assistant.io/lovelace/masonry

### Sections view design
- **Structure:** a view holds **sections** (titled groups), and each section is a **12-column grid** of cards. The **number of card columns inside a section never changes across screen sizes**, "preserving your muscle memory of where the cards are located". Sections reflow left to right ("Z-grid") as width allows. A row is as tall as its tallest section. [V] https://www.home-assistant.io/blog/2024/03/04/dashboard-chapter-1/ ; 12 columns: [V] https://contributors.home-assistant.io/blog/2024/11/06/custom-card-sections-support
- **Edit mode:** a pencil / "Edit dashboard" button at the top right (on an auto-generated dashboard you first "take control" [U]). Done leaves edit mode. [V] https://www.home-assistant.io/dashboards/sections/
- **Moving:**
  - Sections: "Tap and hold the **Move** handle ... Other sections shift to make room."
  - Cards: "Tap and hold anywhere on the card, then move it." The other cards move out of the way live (SortableJS-style list reorder).
  [V] https://www.home-assistant.io/blog/2024/03/06/release-20243/
- **Adding:** an "Add card" button **inside each section** opens a dialog with two tabs:
  - **By card:** card types, with **"Suggested Cards"** (Tile) pinned at the top and a search field.
  - **By entity:** select one or more entities, see a preview, then "Add to Dashboard".
  A device page can add all of a device's entities as tiles in a new section. [V] https://www.home-assistant.io/blog/2024/03/04/dashboard-chapter-1/
- **Resizing (2024.7):** "Resizing is done with a very nice **slider interface**, found in the card editing menu under the new **Layout** tab". You "preview your changes", "Most cards have been given a built-in minimum and maximum size", and there's a revert button. Cards **do not** resize with an on-canvas drag handle. [V] https://www.home-assistant.io/blog/2024/07/03/release-20247/ . Card authors declare `getGridOptions()`: default columns/rows (example: 6 cols x 2 rows) plus min/max. [V] https://contributors.home-assistant.io/blog/2024/11/06/custom-card-sections-support . The Layout tab shows a small grid picker you tap or drag to choose cols x rows, plus a "precise mode" toggle for finer columns. [U, from the frontend PR] https://github.com/home-assistant/frontend/pull/21115
- **Card settings:** a modal dialog with tabs **Config / Visibility / Layout**:
  - Config is a **visual form** with a **live preview** next to it, and "Show code editor" switches to YAML whenever the form can't express something.
  - A whole-dashboard "Raw configuration editor" exists for power users.
  [S] https://github.com/tempus2016/decluttering-card-plus/wiki/Visual-Editors ; Visibility tab conditions [V] https://www.home-assistant.io/dashboards/sections/
- **Density options:**
  - "Max number of sections wide".
  - Dense placement, which "will remove some gaps, but it also means you have less control over the order of the cards".
  - Theme variables for section min width and gap.
  [V] https://www.home-assistant.io/dashboards/sections/ ; [S] https://community.home-assistant.io/t/sections-title-spacing/767353
- **Timeline:** experimental in 2024.3, default view type plus a **Masonry-to-Sections conversion tool** in 2024.11. [S] https://home-assistant.io/blog/2024/11/06/release-202411/
- **Undo:** 2025.10 added undo/redo to the **automation** editor ("Up until now, the only way to revert some unsaved changes made to an automation was to close it and start over"). I found **no dashboard undo**; changes save immediately. [V] https://www.home-assistant.io/blog/2025/10/01/release-202510/

### Complaints about Sections
- "There is a LOT of wasted space on small screens when using the Section title", and it hurts a "7in Raspberry Pi screen" (the same class of screen as OCTAVE's). [S] https://community.home-assistant.io/t/sections-title-spacing/767353
- "On Mobile the button cards get squished together despite there being plenty of space". [S] https://community.home-assistant.io/t/section-view-squishing-cards-together-on-mobile/1011245
- Tiles with 3 or more features leave dead space "almost equivalent to a grid row" (the size quantum is too coarse). [S] https://github.com/home-assistant/frontend/issues/21618
- "the sections idea is nice, but clutters dashboards with tiles" (the defaults push one card style). [S] https://www.smarthomejunkie.net/whats-new-in-home-assistant-november-2024-11-sections-dashboard-webrtc-and-more/
- A max-columns setting stopped being respected on a Nest Hub after an update (layout regressions on fixed wall screens). [S] https://github.com/home-assistant/frontend/issues/28594

### Lessons
- **Predictability beats packing efficiency.**
- A fixed column count per group keeps spatial memory across screens.
- Offer an entity-first ("what do you want to show?") way to add, alongside the card-first way.
- Live preview in the settings dialog.
- A visual form with a raw-text escape hatch.
- Declared min, default and max sizes per card type.

---

## 4. Garmin: data screens and Connect IQ (template-first)

- **On device (Edge 830):** Activity Profiles > profile > Data Screens > screen > "Layout and Data Fields". "**Swipe left or right to change the layout**" (choose how many fields and their arrangement). Then "**Tap a data field, then tap another data field to rearrange them**" (tap-tap swap), and "**Double-tap a data field to change it**". Confirm with the check mark. [V] https://www8.garmin.com/manuals/webhelp/edge830/EN-US/GUID-1BDAFDBC-FBD8-4466-A4D4-5EEEB600A438.html
- **Watches:** each screen has Layout (number of fields) and Data Fields (pick per slot from categorised lists), with Status on/off, Reorder and Add New, where Add New can be "a custom data screen, or ... one of the predefined data screens". "Not all data fields are available for all activity types". [V] https://www8.garmin.com/manuals/webhelp/instinct/EN-US/GUID-638CD68D-11B0-4D9C-B8B7-E28D15EC4566.html , https://www8.garmin.com/manuals/webhelp/GUID-0221611A-992D-495E-8DED-1DD448F7A066/EN-US/GUID-638CD68D-11B0-4D9C-B8B7-E28D15EC4566.html
- **Editing from the phone** (Connect app > device > Activities & Apps > activity > Data Screens) is "much easier and faster than on the watch". [S] https://jdhao.github.io/2024/02/04/garmin_forrunner_setup/ , https://www.tomsguide.com/how-to/change-data-fields-garmin-watch
- **Older Edge 800:** long-press a field to highlight it, then tap again to change it or tap another field to swap. [S] https://forums.garmin.com/sports-fitness/cycling/f/edge-830/377284/edge-800-data-field-changing
- **Connect IQ:** third-party data fields have to render in whatever slot shape the layout gives them, so a field is designed for several slot sizes (one-field, half, quarter, and so on). [U, the UX-guideline page didn't render] https://developer.garmin.com/connect-iq/user-experience-guidelines/data-fields/
- **Complaints:**
  - Firmware updates wiped or changed custom screens: "I've lost all my data screens again, PLUS they've 'improved' the method of setting the fields"; "More time wasted restoring them." [S] https://forums.garmin.com/sports-fitness/cycling/f/edge-explore-820/126348/garmin-leave-my-device-alone/751270
  - Hard limits on screens and fields per screen. [S] https://forums.garmin.com/sports-fitness/sports-fitness/f/edge-520-520-plus/99765/adding-data-screens
- **Lessons:**
  - Pick a layout template, then fill named slots.
  - Tap-tap swap instead of drag.
  - The field picker is categorised and filtered to what the context supports (OCTAVE's "vehicle supports this PID" gate).
  - Layouts must survive app updates.
  - Offer a phone-side editor for comfort.

---

## 5. Grafana, react-grid-layout, Figma / FigJam, Canva, Notion

### Grafana
- **Grid:** 24 columns. `h` is in 30 px units. "the grid has **negative gravity** that moves panels up if there is empty space above a panel" (vertical compaction). [S/V, Grafana docs] https://grafana.com/docs/grafana/latest/reference/dashboard/
- **Interaction:** drag a panel by its header, resize from the bottom-right corner. Other panels are pushed down live and float back up. Built on react-grid-layout. [U, well known]
- **Dynamic dashboards (Grafana 12, preview):**
  - **Auto grid layout**: the panel size comes from a maximum column count (default 3) and a column-width mode (narrow / standard / wide / custom), so you choose density instead of placing every panel by hand.
  - A **custom grid** option for manual placement.
  - A dockable **edit pane** that shows settings for whatever is selected.
  - A clickable **content outline** (tree).
  [V/S] https://grafana.com/docs/grafana/latest/observability-as-code/schema-v2/layout-schema , https://grafana.com/docs/grafana/next/visualizations/dashboards/build-dashboards/create-dashboard , https://grafana.com/whats-new/2025-04-10-dynamic-dashboards/
- **Touch complaints:**
  - "It is near impossible to scroll on a dashboard on a phone or tablet without moving around a panel ... I place my finger in the small gap between panels and drag ... I moved a panel." The user asked for a quick lock toggle because "the read-only mode is there, it just isn't readily available". [V] https://community.grafana.com/t/quick-lock-edit-mode-of-dashboards-for-tablet-view/70191
  - **Grafana is removing dashboard edit mode on phones from 2026-11-05**: "editing on a phone increasingly lags behind what you can do on a tablet or desktop"; "tablets keep full dashboard edit mode". [V] https://grafana.com/whats-next/2026-11-05-dashboard-edit-mode-is-going-away-on-mobile-phones/
- **Lessons:**
  - Never let a scroll gesture start a move. Moves happen only in an explicit edit mode, and editing is separate from viewing.
  - A density-driven auto layout is a good default; manual placement is the advanced option.

### react-grid-layout (the de facto web grid engine; Grafana, many others)
- **Collision policy is a setting:**
  - `compactor`: vertical, horizontal or none (the old `compactType`).
  - `preventCollision`: "Block movement into occupied space instead of pushing items".
  - `allowOverlap`.
- **Resize handles:** any of `s w e n sw nw se ne`, default `['se']` only.
- **Responsive layouts:** breakpoints `{lg:1200, md:996, sm:768, xs:480, xxs:0}` with cols `{12,10,6,4,2}`. **A separate layout is stored per breakpoint.**
- Optional drag `handle` / `cancel` selectors. Default `rowHeight` 150, margin [10,10].
[V] https://github.com/react-grid-layout/react-grid-layout
- **Lesson:** the four policies are push (compact), block (forbid), swap, and overlap. Pick one per product deliberately. Storing a layout per breakpoint is how the web handles orientation and screen class.

### Figma and FigJam on iPad (touch canvas)
- Figma request: on iPad a one-finger drag on blank space starts a marquee and can move items in the same gesture, which "can ruin a design by accident". The poster asks for FigJam's behaviour: **an object must be deliberately tapped (selected) before it can be dragged**, and one finger on empty space pans. [V] https://forum.figma.com/suggest-a-feature-11/prevent-select-and-drag-with-a-single-tap-on-ipad-like-figjam-app-42995
- FigJam iPad: **two-finger tap = undo, three-finger tap = redo**. [S] https://forum.figma.com/archive-21/launched-figjam-on-ipad-undo-and-redo-32542 . Users of the Figma design app ask for the same and complain about missing undo buttons. [S] https://forum.figma.com/report-a-problem-6/undo-and-redo-buttons-gone-on-figma-app-on-ipad-58610

### Canva mobile
- Tap to select. Corner and side handles appear. Smart guides (magenta lines) show when edges or centres line up. A bottom toolbar has a **Position** sheet for exact values and for arrange / align. Desktop has a toggle to turn snapping off. [S] https://www.canva.com/de_de/help/resize-and-crop/ , https://supademo.com/blog/how-to-resize-on-canva . Holding to disable snapping on mobile: [U]

### Notion (blocks)
- Desktop: a six-dot "⋮⋮" handle on hover (drag to reorder, click for the menu). Mobile: long-press a block for its action menu, and on mobile blocks are reordered by dragging from an edit or handle affordance rather than anywhere. [S/U] https://productivityheadquarters.substack.com/p/understanding-notion-the-building , https://thebricks.com/resources/how-to-turn-a-block-into-something-else-on-notion-mobile
- NN/g warns that grab-handle icons "are not nearly as universal as designers may think", so pair the icon with a lift animation or a hint. [V] https://www.nngroup.com/articles/drag-drop/

---

## 6. Stream Deck, Touch Portal, TouchOSC, and mobile game HUD editors

### Elgato Stream Deck
- **Desktop editor:**
  - A **fixed key grid** mirrors the hardware. Drag an action from a right-hand categorised and searchable list onto a key, or drag keys to move or swap them.
  - A **property inspector** at the bottom shows only the selected key's settings, and its height can be resized ("By increasing its height, you can see more properties without needing to scroll.").
  - Folders and pages handle overflow, and profiles can switch automatically per app.
  [S] https://www.elgato.com/explorer/products/stream-deck/elgato-stream-deck-quick-start-guide/ , https://help.elgato.com/hc/en-us/sections/5162671529357-Elgato-Stream-Deck-Software-Release-Notes
- **Stream Deck Mobile:**
  - The phone is mainly the surface. Actions are configured on the computer.
  - A custom keypad size is a Pro feature. When the grid shrinks, "existing keys will not be deleted. The keys will be used in order starting from the upper left corner".
  [S] https://help.elgato.com/hc/en-us/articles/16549072466445-Elgato-Stream-Deck-Mobile-2-0-How-to-change-Keypad-Layout
- **Lesson:** a fixed grid is easy and quick for a big audience. When the grid shrinks, nothing is destroyed; items reflow in reading order.

### Touch Portal
- Editing happens on the **desktop**; the phone or tablet is the live surface: "when you edit a page which is the current active page on the mobile device, you will see the changes."
  - Drag premade controls onto the page. Some ask for options before they're added.
  - Drag buttons to move them, and click a button or an empty slot to edit it.
  - Ctrl+A / copy / paste for buttons.
  [V/S] https://www.touch-portal.com/blog/post/tutorials/main-window-explanation-v3.php , https://www.touch-portal.com/blog/post/tutorials/all-touch-portal-hotkeys.php
- **Grid:** a columns x rows picker (the free tier is fixed at 4x2). "All buttons will stay on their location within the grid but the grid expands so the buttons [become] a bit smaller." [V] https://touch-portal.com/blog/post/tutorials/changing-page-sizes.php

### TouchOSC (hexler)
- The editor runs on every platform, phones included.
- A grid with a configurable resolution and a **snap toggle**.
- Multi-select with **Align** (2 or more controls) and **Distribute** (3 or more) in the mk1 manual.
[S] https://hexler.net/touchosc/manual/preferences-editor , https://hexler.net/touchosc-mk1/manual/editor-controls-edit

### Mobile game HUD editors (PUBG Mobile, CoD Mobile, Fortnite): hundreds of millions of touch users
- **Entry:** Settings > Controls > Customize (PUBG) or the cog next to "Customise" (CoDM). Editing happens on a **frozen full-screen replica of the real HUD**. [S] https://www.hardreset.info/devices/apps/apps-pubg/customize-controls/ , https://www.ggrecon.com/guides/call-of-duty-mobile-layout-and-sensitivity-guide
- **Interaction:**
  - Tap an element to select it, then drag it freely, so the finger lands where it will in play.
  - A **floating toolbar with Size and Transparency sliders** acts on the selected element, with values as percentages (for example, joystick 110-120%, transparency 70-80%).
  - Save or Cancel.
  - Several **layout slots** (3 in PUBG).
  - A **reset to default**.
  - Ready-made presets such as "Large Icons".
  [S] https://www.redbull.com/ie-en/pubg-mobile-tips-guide , https://www.sportskeeda.com/esports/how-reset-pubg-mobile-settings , https://lootbar.gg/blog/en/best-hud-layout-guide-2-3-4-finger-setup-pubgm.html
  - Arrow nudge buttons: [U] (seen only in a weak Android Central snippet)
- **Sharing:** PUBG has **layout codes**, so a player can import someone else's layout by code and browse **featured community layouts** inside the editor. [S] https://lootbar.gg/blog/en/best-hud-layout-guide-2-3-4-finger-setup-pubgm.html
- **Fortnite HUD Layout Tool:**
  - Separate layouts per mode (Combat / Build).
  - The **hit area can be resized separately from the drawn icon size**.
  - Removed elements "are always available to be restored".
  [S] https://accessible.games/accessible-player-experiences/access-patterns/personal-interface/
- **Lesson:**
  - **Select, then manipulate with a floating contextual toolbar.**
  - Edit a WYSIWYG replica.
  - Slots and presets, reset to default, and shareable codes.
  - Iterate between edit and use (players are told to test in the training ground).

---

## 7. Cross-product comparison

| Concern | iOS | Pixel/Nova | KWGT | Home Assistant Sections | Garmin | Grafana / RGL | Game HUDs |
|---|---|---|---|---|---|---|---|
| Enter edit | long-press (menu or jiggle) | long-press | separate editor app | explicit Edit button | menu path | Edit button | menu > Customize |
| Add | gallery: search, live previews, sizes grouped | picker: search, per-app cards | item list plus presets | in-section Add card: by card or by entity, suggested | Add New: custom or predefined screen | Add panel | n/a (fixed set, show or hide) |
| Move | drag after lift | drag after lift | numeric offsets | hold and drag, live reorder | tap-tap swap | drag header | select, then drag |
| Resize | corner bar snapping to declared sizes | 4-side dots, cell snap | numeric | Layout-tab slider or grid picker with min/max | layout template | SE corner | size slider |
| Collision | push and reflow (packed) | push into free cells, else refuse | free overlap (layers) | list reorder | n/a (template) | push down plus gravity | free overlap |
| Settings | flip-to-back mini form | app's config activity | huge bottom panel | dialog: form, preview, YAML escape | slot picker | edit pane | floating sliders |
| Undo | none | none | yes (editor) [U] | none for dashboards | none | browser undo [U] | Cancel / Reset |
| Templates | Smart Stack, suggestions | none | presets marketplace | auto-generated, by device | predefined screens | library panels | presets, slots, codes |

---

## 8. Top 15 interaction patterns OCTAVE should adopt (ranked by impact)

1. **Template-first creation ("pick a layout, then fill slots").** New Dashboard opens a gallery of starting layouts (1 big plus 4 small, 2x2, 3x3, a 6-field "Garmin" grid, Race, Eco, Media plus gauges), rendered as live previews with simulated data. After that, each empty slot is a big "+" that opens the widget/PID picker. This covers what most users want with no dragging at all. Sources: Garmin layouts, Apple Watch slots, HA by-entity add, PUBG presets.
2. **An explicit, sticky edit mode, plus an edit lock while driving.** Nothing on a viewing dashboard can move. Enter edit mode with a clear button (and optionally a long press of at least 500 ms that opens a menu with "Edit dashboard", never a direct jiggle). Leave with a large Done. Disable editing, or ask for confirmation, when vehicle speed is above 0. Sources: Grafana scroll-moves-panel complaint and phone edit removal, iOS accidental jiggle, NHTSA glance limits.
3. **Select first, then manipulate (FigJam / game-HUD model).** Tap selects. Only a selected widget can be dragged or resized, and on an unselected widget a drag scrolls or pages. A selected widget shows a floating contextual toolbar next to it, never under the finger: Settings, Size presets, Duplicate, Delete, Swap. This kills accidental moves without timing tricks. Sources: Figma/FigJam iPad, PUBG/CoDM, Canva.
4. **Undo/redo with a visible button, plus Cancel and Save for the whole session.** Edit a draft copy; Save commits and Cancel discards. Keep an in-session undo stack (a 50-step ring is enough) with toolbar buttons, plus two-finger-tap undo as a bonus. Show "Undo" on a snackbar after Delete instead of a confirm dialog. Sources: Apple HIG "Prefer letting people undo a drag-and-drop operation"; no undo is a gap in HA and iOS; FigJam gestures.
5. **Size presets that snap, with handles as the secondary way.** Each widget type declares `minSpan / defaultSpan / maxSpan` plus a short list of good sizes (S/M/L/W/T) in `widget-catalog.json`. The toolbar has S/M/L chips, and dragging the grip snaps only to declared sizes, with a live "3 x 2" readout shown away from the finger. Sources: iOS 18 corner bar snapping to declared sizes, HA min/max via `getGridOptions`, Android `min/maxResize`.
6. **Live push-aside reflow while dragging, not a reject and snap-back.** As the dragged widget's centre crosses a neighbour's edge, animate the neighbours (about 100 ms, eased) into the nearest free cells, or **swap** them when the sizes are equal. Show a drop-target ghost under the finger. Only when no arrangement fits, tint the ghost red and spring back on release. The current "forbid overlap and snap back" rule is the anti-pattern users hate least, but it is still uninformative. Sources: NN/g, Apple HIG feedback, Pixel Launcher reorder, RGL policies.
7. **Tap-tap swap and move.** With a widget selected, tapping another widget offers "Swap" (or swaps directly in a Swap mode), and tapping an empty cell offers "Move here". This is precise, needs no drag, and is accessible. Source: Garmin Edge "Tap a data field, then tap another data field to rearrange them".
8. **Widget gallery as a bottom sheet with search, categories and live previews.**
   - Categories: Gauges / Readouts / Media / Status / Clock.
   - A "Suggested for your car" row, filtered by supported PIDs.
   - Every widget type shows **one card with its sizes grouped**, plus a one-line, verb-first description.
   - Tap adds the widget to the first free slot, or the selected empty cell, and then selects it.
   - Dragging from the sheet is optional.
   Sources: Apple widget HIG, Android 12 picker, HA suggested cards.
9. **Data-first add ("What do you want to see?").** Next to the widget gallery, a PID-first path: pick Coolant Temp and OCTAVE proposes the best widget for its `kind` and range, then places it. Source: HA "By entity" add plus auto card suggestion. This plays to OCTAVE's catalog `supportedKinds`.
10. **Big, offset handles with generous hit areas.**
    - The resize grip has a hit area of at least 48 dp / 10 mm (aim for about 12-15 mm in a moving vehicle; the automotive literature says 19-22 mm for driving use), drawn outside or at the corner of the widget.
    - Drag starts after about 3 pt / 8 dp of movement.
    - Keep the finger-to-object offset constant during the drag.
    - On a small widget, the toolbar's size chips stand in for the handle.
    Sources: Apple HIG, Material, NN/g, Android touch slop, Holz & Baudisch, Feng 2018.
11. **A short settings sheet, with an "Advanced" section folded away.** The settings sheet shows 3 to 5 essentials: data (PID), label, range or auto, colour/theme accent. Everything else in `editableProps` goes under an "Advanced" disclosure. Preview the widget live at the top of the sheet. Power users get "Edit JSON" (the YAML-escape equivalent). Sources: Apple widget HIG (few configuration options, flip-to-edit), HA's form, preview and YAML; KWGT's overwhelming bottom panel as the counterexample.
12. **Per-orientation / per-screen-class layouts with predictable derivation.** Keep the column count fixed within a layout so spatial memory survives. Store separate landscape and portrait (or wide and narrow) layouts. When one is missing, derive it once in reading order (Stream Deck: "keys will be used in order starting from the upper left corner") and then let the user adjust it. Never re-pack masonry-style at runtime. Sources: HA Sections rationale, RGL per-breakpoint layouts, Stream Deck, Touch Portal.
13. **A user-selectable grid density, with a half-step option.** Offer Coarse / Normal / Fine grids (for example 4x3, 6x4, 8x6 on a 7" screen; make sure fine still meets the 10 mm target rule), or Nova-style subgrid for experts. Changing density keeps relative positions, like Touch Portal. Sources: Nova subgrid, Touch Portal grid picker, TouchOSC grid resolution.
14. **Reset, duplicate-before-edit, and shareable layout codes.**
    - "Reset to default" on built-in presets (they stay read-only, and editing one forks a copy).
    - "Restore removed widget".
    - Export and import as a short code or QR. The share and import flow already exists; make it a first-class "Get layouts" entry with featured community or bundled layouts.
    - Layouts must survive app updates (migrate the schema, never wipe).
    Sources: PUBG layout slots and codes, Fortnite restore, Garmin firmware-wipe complaints.
15. **Edit from a second device with a live preview on the car screen.** Because OCTAVE already serves `dashboardManager` over the service API, a phone or laptop editor (web SDK) that changes the car's dashboard live is the Touch Portal / CarPlay / Garmin Connect pattern. It is the comfortable way to edit when the car screen is awkward to reach. Sources: Touch Portal live edit, CarPlay widget settings on iPhone, Garmin Connect "much easier and faster", Grafana moving editing to bigger screens.

Honourable mentions:
- Haptic or audible tick on lift, snap and drop, where the hardware allows it (NN/g).
- Auto-page when a widget is dragged to the screen edge between dashboards, with a 500 ms+ dwell so it isn't accidental (the iOS complaint "drag an app past the screen you want").
- Multi-select with align and distribute only for desktop/mouse (TouchOSC).
- A one-time coach mark ("Tap to select, drag to move, drag the corner to resize").

## 9. Anti-patterns to avoid

1. **Drag-anywhere-to-move in view mode, or a drag that starts on the first touch.** It turns scrolling and swiping between dashboards into accidental edits. Source: Grafana "I moved a panel"; Figma iPad marquee-plus-move.
2. **A long press that drops straight into a fragile edit mode** with no way out other than finding Done, and no lock. Source: iOS jiggle complaints, "Lock the iPad icons" request.
3. **Unpredictable auto-packing (masonry or gravity reflow)** that moves widgets the user didn't touch, or moves them differently on another screen. Source: HA Masonry post-mortem; iOS "icons jumping out of the way unpredictably"; iOS 14 folder loss.
4. **Silent rejection:** a drop that just snaps back with no hint of why (the current OCTAVE behaviour). Show the ghost turning red and a reason ("No room: make space or resize"). Source: Apple HIG invalid-drop feedback.
5. **No undo, or destructive actions behind confirmation dialogs only.** Dialogs get tapped through; undo is safer and quicker. Source: Apple HIG; HA dashboards; iOS.
6. **Free pixel sizing, or a size quantum so coarse it wastes a row.** Snap to sizes that look good; avoid the HA tile dead-space problem. Source: HA issue 21618; Apple "Avoid expanding a smaller widget's content to simply fill a larger area".
7. **Tiny, unlabeled icon toolbars and a cramped bottom properties panel that steals the canvas** (KWGT). Use labelled chips and a sheet that can expand to full height, with the selected widget's preview pinned. Source: Pratt critique; Kustom forum.
8. **Exposing every property at once,** or making code or formulas the main path. Source: KWGT critique; Apple widget HIG on minimal configuration.
9. **Handles inside small widgets, or under the finger,** and drawn hit areas that are only the size of the glyph. Source: NN/g handle guidance; Material 48 dp; Hoober's edge-accuracy data.
10. **Treating long press as the only way to reach an action** (it's undiscoverable). Always also have a visible button. Source: Meta touch guidance; NN/g.
11. **Relying on multi-finger gestures (pinch to resize, two-finger rotate) as the primary path** on a car screen held at arm's length. Offer them only as shortcuts. Source: inferred from automotive target-size research [U].
12. **Losing user layouts on update, or silently changing them on update.** Source: Garmin Edge firmware complaints.
13. **Wasted chrome on small screens** (section titles, big gaps) on 7" panels. Let users collapse the editor chrome and choose gap or density. Source: HA "LOT of wasted space ... 7in Raspberry Pi screen".
14. **Allowing overlap by default.** Keep it off; at most an expert toggle with bring-to-front and send-to-back. Source: Nova makes overlap opt-in; HA, iOS and Android launchers forbid it.
15. **Editing on the car screen while the vehicle is moving.** Gate it. Source: NHTSA 2 s / 12 s glance guidance.

---

### Gaps and unverified items worth checking hands-on
- The exact iOS 18 long-press widget-menu size icons, and whether a resize can be undone.
- The current KWGT preview drag support.
- Whether HA's Layout tab "precise mode" is current.
- PUBG/CoDM nudge arrows.
- Pixel's tap-to-add in the widget picker.
- Notion mobile reorder gesture.
- Hoober's corner-accuracy numbers.
- Garmin Connect IQ data-field UX page (didn't render).
