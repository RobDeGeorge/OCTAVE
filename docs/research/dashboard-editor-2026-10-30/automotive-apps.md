# Competitive study: dashboard / gauge-layout editors in automotive apps

Prepared 2026-10-30 for the OCTAVE dashboard-editor audit.

## Method and caveats

- Sources: official manuals and help pages, vendor forums (RealDash, Torque, MoTeC, Ecumaster), owner-club forums, App Store pages, review aggregators, and changelogs. Each claim has its source URL beside it.
- **Reddit could not be fetched or searched**: the tooling blocks reddit.com. Owner-club forums and app-store reviews stand in for it.
- **Google Play pages could not be read** (the page body was truncated or returned 404). Install and rating numbers come from third-party trackers (apppricinglab, grand-screen) and are snapshots that disagree with each other. Treat them as orders of magnitude.
- Some forums (chevybolt.org, silveradosierra.com, pajeroforum, priuschat) block automated readers. Where a claim rests only on a search-engine snippet of such a page, it is marked "(snippet only)".
- Claims I could not check against a primary source are marked **unverified**.

Quick reference for OCTAVE's current editor, from a read of `frontend/dashboards/editor/`:
- Placement is a 12-column grid (`gridColumns` / `gridRows` in the spec): tap to place, drag to move, a corner grip to resize.
- The PID picker is searchable, grouped by kind, and sorts supported PIDs first.
- A draft is stashed when the editor closes with unsaved changes, and a two-tap "Discard?" guards Cancel.
- I found no undo / redo and no per-orientation variant of a dashboard.

---

## 1. RealDash (the most important competitor)

**Scale.** About 0.5–0.9 M installs on Google Play and roughly 3.5–3.6 stars from about 3.7–3.8 K ratings ("Mixed or Average"); the trackers disagree ([grand-screen](https://grand-screen.com/apps/realdash/), [apppricinglab](https://www.apppricinglab.com/app/google_play/com.napko.RealDash)). Also on iOS, Windows, Linux and macOS. Free with in-app purchases; the optional "My RealDash" subscription unlocks premium content on every platform and syncs files ([forum: Premium dashboard](https://forum.realdash.net/t/premium-dashboard/648)).

**Creating a dashboard.** You almost always start from an existing dashboard: a built-in one, a gallery download, or an import. Then you tap "Edit" in the top menu ([forum: Editing gauges](https://forum.realdash.net/t/editing-gauges/1514)). The gallery "has a ton of free and premium dashboards to download" ([grand-screen listing](https://grand-screen.com/apps/realdash/)). I found no "new blank dashboard" wizard in the docs (**unverified** whether one exists).

**Adding a widget.** Edit mode has a menu bar with these panels: File, Add Gauge, Look'n Feel, Input & Values, Dash Info and Settings, each with a keyboard shortcut (Shift+1 to Shift+6) ([manual: keyboard shortcuts](https://realdash.net/manuals/keyboard_shortcuts.php)). Example from the official tutorial: "Add Gauge > Image", then set the image under Look'n Feel > Images > Background Image ([manual: make an indicator](https://realdash.net/manuals/make_an_indicator.php)). A real gauge is a stack of separate primitives; a user converting Boost to MAP had to select and rename each part (arc, text, value, unit) on its own ([forum: Basic gauge editing for dummies](https://forum.realdash.net/t/basic-gauge-editing-for-dummies/1519)).

**Placement model.** Free-form x/y with layers. The full desktop toolset ([keyboard shortcuts](https://realdash.net/manuals/keyboard_shortcuts.php)):
- Clipboard and selection: undo (Ctrl+Z), cut / copy / paste, select all / none, Tab to cycle through gauges.
- Alignment: align left / right / top / bottom, space evenly, centre (H / V / both).
- Sizing: match size and position, make the same size, "resize to image size (pixel perfect)", make fullscreen, scale all up / down.
- Nudging: arrow keys move, Shift+arrows resize, Ctrl+Shift+arrows resize around the centre.
- Layers and locking: numpad +/- change z-order, Ctrl+F / Shift+Ctrl+F freeze / unfreeze gauges (a lock).
- Pages: delete the current page (Shift+Del).

On a busy dashboard, edit mode has a gauge-selection list on the left behind a narrow handle ([manual: change text colour](https://realdash.net/manuals/change_color_of_text_gauges.php)). That panel covers the left side of the dash. Users had to be told to "press the little blue stripe" to hide it, and the developer pointed to Ctrl+W to hide every edge menu ([forum thread 5490](https://forum.realdash.net/t/how-do-you-see-the-stuff-on-the-left-side-of-the-dashboard-in-edit-mode/5490)).

**Widget customisation.** Two property panels, Look'n Feel and Input & Values, each opened from the menu bar.
- Look'n Feel holds colours, images, fonts and text, and "Special" settings. Special includes Angles and Offsets (start angle, sweep) and Auto Scaling (tick numbers, digits, segments) ([forum: Dash design for beginners, ARC gauge](https://forum.realdash.net/t/dash-design-for-beginners-arc-gauge/7086)).
- Custom artwork is a first-class feature: an ARC gauge is up to five PNG layers (background, arc, segment, mid segment, mask). The arc is drawn white so the app can tint it per level (same thread).
- **Warning model:** every gauge has Range (clamp), Warning band and Critical band, all set in Input & Values. Visual properties are edited per level through an "Editing Level" selector (Normal / Warning / Critical / ALL) ([manual: normal, warning and critical levels](https://realdash.net/manuals/using_normal_warning_and_critical_levels.php)).
- Trap reported by a dashboard author: "If you do an edit while Editing Level is ALL, this will delete the colors assigned" ([ARC gauge thread](https://forum.realdash.net/t/dash-design-for-beginners-arc-gauge/7086)).
- Indicators work the same way. An image is bound to an input and blended almost transparent at Normal, so it "appears" at Warning or Critical ([make an indicator](https://realdash.net/manuals/make_an_indicator.php)).

**Data binding.** Input & Values > "Select Data Source", then pick a category such as Engine/ECU inputs ([Editing gauges](https://forum.realdash.net/t/editing-gauges/1514)).
- Computed channels live in the XML CAN or OBD description files. A `conversion="V/10"` style formula has access to bytes (`B0`, `B1`), other inputs (`ID64`), named custom values, bit operations and maths functions. `conversionABC` uses A/B/C letters "to make formulas from other apps easier to port" ([RealDash-CAN description spec](https://github.com/janimm/RealDash-extras/blob/master/RealDash-CAN/realdash-can-description-file.md)).
- Units are declared in that XML (C/F/K, km/h/mph, bar/psi, time, bit) for automatic conversion (same spec).
- A `name=` attribute creates a custom input in an "ECU Specific" category. A dashboard using custom inputs needs the same XML on every machine, "otherwise the dashboard can use built-in inputs with the **Input Mapping** feature" (same spec). This is a cross-vehicle portability mechanism.

**Preview / live data.** Edits happen over the live running dash (**unverified** whether a simulated-input sweep exists in the editor).

**Undo.** Ctrl+Z exists on desktop ([shortcuts](https://realdash.net/manuals/keyboard_shortcuts.php)). I found no touch undo button in the docs (**unverified**).

**Save and versioning.** Save is Ctrl+S, plus a per-edit checkmark ([Basic gauge editing](https://forum.realdash.net/t/basic-gauge-editing-for-dummies/1519)). There is no versioning. The developer's standing advice is to "make backups" because editing "can get confusing" ([Editing gauges](https://forum.realdash.net/t/editing-gauges/1514)). Lost-work reports:
- A save bug in 2.0.2 corrupted a dashboard so it would not load in 2.0.3 ([forum: Save file corrupt](https://forum.realdash.net/t/save-file-corrupt-help/1163), snippet only).
- "Failed to save" on an older Android head unit ([forum: Unable to import or save edited dash layouts](https://forum.realdash.net/t/unable-to-import-or-save-edited-dash-layouts/10819), snippet only).

**Pages.** Multi-page; arrow keys switch pages in run mode ([shortcuts](https://realdash.net/manuals/keyboard_shortcuts.php)).

**Screen sizes and aspect ratio (a major pain point).** A dashboard is bound to the aspect ratio it was saved at. The developer (2020): "the original aspect ratio must be maintained to preserve the shape and relation of the gauges ... There really is no way around this." A user who edited at a non-16:9 ratio found the dash "will never again display correct on 16:9". The workaround is an undocumented `[NOSCALE]` tag under Look & Feel > Special. The developer floated "A warning message maybe when you start editing in different aspect?" ([forum: Aspect Ratio Kills dash](https://forum.realdash.net/t/aspect-ratio-kills-dash/290)). Another user reported the edit button disappearing in fullscreen ([forum: Fullscreen issues](https://forum.realdash.net/t/fullscreen-issues/6475), snippet only).

**Sharing.**
- One `.rd` file works on every platform ([Premium dashboard](https://forum.realdash.net/t/premium-dashboard/648)).
- On Android 10+ you must use Load > Import, because of scoped storage ([forum: Moving a dash from Windows to Android](https://forum.realdash.net/t/moving-a-dash-from-windows-to-android/1026)).
- The gallery has a "Community" section. Users share to themselves on My RealDash so the file shows up in the Gallery on another device (same thread). Community dashboards are imported via Edit > File > "import from realdash" ([forum: Dashboard Download from community](https://forum.realdash.net/t/dashboard-download-from-community/6286)).
- Premium dashboards bought on Google Play do not work on Windows or Apple ([Premium dashboard](https://forum.realdash.net/t/premium-dashboard/648)).
- A third party built "RealSkin", an AI web service that edits `.rd` files from plain-language prompts. The author says the format is "difficult to build or edit from scratch". The developer warned that "cracking our proprietary file format takes this into gray area" ([forum: RealSkin](https://forum.realdash.net/t/online-skin-builder-for-realdash-realskin/10931)). Demand for a gentler editor is strong enough that people build one outside the app.

**Touch ergonomics (the headline finding).** Asked about editing on a head unit, RealDash's own developer said: "Its just too clumsy to do on touch screen", and recommended the desktop build. Asked for a manual: "Not really, you need to experiment on most cases" ([forum: RealDash online manual?](https://forum.realdash.net/t/realdash-online-manual/8345)).

**Praise.**
- "Absolutely endless options and customizations" (Play review, [grand-screen](https://grand-screen.com/apps/realdash/)).
- The setup is "a bit more complex than most of the users have used to, but in exchange it allows to create very complex dashboards" ([MSExtra forum](https://www.msextra.com/forums/viewtopic.php?t=60789&start=120), snippet only).
- Once learned, a beginner found it "rather fun" ([Basic gauge editing](https://forum.realdash.net/t/basic-gauge-editing-for-dummies/1519)).

**Complaints.**
- A user found the learning curve "quite steep" even for "basic editing of the gauges" ([search snippet of RealDash forum](https://forum.realdash.net/t/basic-gauge-editing-for-dummies/1519)).
- "I'm a beginner and it certainly is confusing", and the tutorials didn't make the steps clear ([Editing gauges](https://forum.realdash.net/t/editing-gauges/1514)).
- "Pretty intimidating at first" ([Basic gauge editing](https://forum.realdash.net/t/basic-gauge-editing-for-dummies/1519)).
- A new user hunted through "pull up and pull down menus" to find delete ([online manual thread](https://forum.realdash.net/t/realdash-online-manual/8345)).
- "Easter eggs can be a little bit hard to unscramble" (Play review).
- "Cannot load .rd file after..." (Play review, truncated) ([grand-screen](https://grand-screen.com/apps/realdash/)).
- RPM text overlapping on the default dash with a Haltech ECU, unsolved in the thread ([Basic gauge editing, 2026 post](https://forum.realdash.net/t/basic-gauge-editing-for-dummies/1519)).

---

## 2. Torque Pro (Android)

**Scale.** About 1 M to 4.2 M installs (trackers disagree) and 4.0–4.1 stars from about 80 K ratings; paid, about USD 4.95 ([grand-screen](https://grand-screen.com/apps/torque-pro-obd-2-car/reviews/), [apppricinglab](https://apppricinglab.com/app/google_play/org.prowl.torque)).

**Creating and adding.** The "Realtime Information" screen holds pages of gauges, with "up to 7 pages" mentioned (search snippet of [theonegauge.com](https://www.theonegauge.com/knowledge-base/torque-pro-setup/), snippet only). To add a gauge, long-press an empty area > "Add display" > choose a type (dial, half dial, bar, graph, digital) > scroll to the sensor ([mopar1973man](https://mopar1973man.com/forums/topic/14570-electrical-article-using-torque-pro-for-gauges-extended-pid-sensors/), snippet only; [ecomodder review](https://ecomodder.com/forum/239153-post1.html), snippet only). Long-press a gauge for a menu with delete / move / add / "Display Configuration", which covers size, location, min/max and high/low warnings ([theonegauge](https://www.theonegauge.com/knowledge-base/torque-pro-setup/), snippet only; [ramforum](https://www.ramforum.com/threads/tablet-and-apps-for-navigation-with-odb-data-overlays.190320/post-2671420), snippet only).

**Placement.** Grid placement; snapping to other controls was missing until 2021.
- A user asked for snapping to other controls, multi-select and edge alignment.
- The developer (admin "piemmm", Dec 2021) said snap-to lines were a priority and "mostly done". He added that they "don't work too well with different sized displays yet", because layouts depend on "screen sizes and dpi".
- A user suggested centring rather than resizing a layout on a larger display ([Torque forum: Dashboard Design](https://torque-bhp.com/community/main-forum/dashboard-design/)).

**Customisation and themes.**
- Themes are a zip containing `properties.txt` (key=value) plus `background.jpg`, `dial_background.png`, `display_background.png`, optional fonts and a logo.
- A per-sensor dial face can be named by PID hex (`dial_background_0c.png`).
- Dial angles, needle size ratio, colours and a global font scale are configurable. Import is from the theme chooser.
- The wiki says the newer theme options "affect round dial type only" so far ([Torque wiki: Themes](https://wiki.torque-bhp.com/view/Themes)). Themes are global art, separate from layout.

**Data binding.** Picked from a flat scrolling sensor list. Custom PIDs use hex mode/PID plus an equation; plugins add manufacturer-specific sensors ([obdadvisor review](https://obdadvisor.com/torque-pro-review/)). Whether unsupported sensors are hidden from the list is **unverified**.

**Saving and sharing.** Layout Settings > Export / Import, with files in `.torque/dashboards/` (search snippets of [priuschat: Share your Torque layouts](https://priuschat.com/threads/share-your-torque-layouts-here.149140/) and the chevybolt thread, snippet only). Forum culture is to share layout files and screenshots.

**Lost work.**
- "If you enter a bunch of displays/gauges, and THEN you change the number of Pages ... chances are you are going To LOOSE SOME OF YOUR DISPLAYS AND GAUGES" ([chevybolt: Step by step needed](https://www.chevybolt.org/threads/step-by-step-needed-for-setting-up-torque-pro.36750/), snippet only).
- "every time I open the app the dashboard has gone and app is back to factory settings"; storage permission was the suspected cause ([pajeroforum: gauges not saved](https://www.pajeroforum.com.au/forum/general-information/electrical-radios/1070744-torque-pro-app-gauges-not-saved), snippet only).

**Orientation and screen sizes.** Layouts depend on screen size and DPI, per the developer quote above. A separate portrait / landscape layout is **unverified**.

**Praise.** "Still the deepest customization in the OBD2 app market" ([obdadvisor](https://obdadvisor.com/torque-pro-review/)).

**Complaints.**
- "Interface looks like it was designed in 2012 (because it was)".
- The customisation "has a real learning curve": first-time users often spend "15–20 minutes of YouTube tutorials" ([obdadvisor](https://obdadvisor.com/torque-pro-review/)).
- "Interface can be cluttered and confusing to navigate" (editorial on [grand-screen](https://grand-screen.com/apps/torque-pro-obd-2-car/reviews/)).
- A busy realtime screen at arm's length "is too busy to be useful" ([ecomodder](https://ecomodder.com/forum/239153-post1.html), snippet only).

**Android Auto.** Not native. The community AA-Torque project offers "up to 10 different dashboards" and EvalEx expressions such as `(1000 / a)`, and warns that a "layout designed for another vehicle may contain gauges that do not work on your car" ([aahacks](https://blog.aahacks.com/torque-pro/)).

---

## 3. Car Scanner ELM OBD2

**Scale.** The largest consumer app in this set: about 10 M Play installs, 4.7 stars from about 300–325 K ratings, and 4.8 stars from about 30–33 K ratings on iOS ([apppricinglab](https://www.apppricinglab.com/app/google_play/com.ovz.carscanner), [App Store](https://apps.apple.com/us/app/car-scanner-elm-obd2/id1259933623)).

**Creating a dashboard.** Template-first.
- Dashboard > gear > "Add Page" > **choose a layout** > tick "+" beside each value. "Only the values that can be displayed on your car will appear."
- Then gear > "Customize Page" opens the Dashboard Editor, where you "move the position of the gauges on the layout you selected". A gear on each item opens the Item Editor: background / border, text size / colour, dial settings, min / max / average, warnings and alarms ([obd2australia guide](https://obd2australia.com.au/car-scanner-elm-obd2-app-dashboard/)).

**Placement.** A layout skeleton first, then move and size inside it. An iOS reviewer: "user friendly GUI display builder ... Allows placing and sizing and it doesn't need to be connected to build" ([App Store review, Gms627, 2025-04-20](https://apps.apple.com/us/app/car-scanner-elm-obd2/id1259933623)).

**Customisation.**
- Widget types include digital, bar, graph and gauge, plus a multi-sensor histogram added in 1.89.8 ([apkmirror changelog](https://apkmirror.com/apk/0vz/car-scanner-elm-obd2/car-scanner-elm-obd2-1-91-2-release)).
- "Set your own yellow and red limits, data ranges, colors and warning tones" (same App Store review).
- There is a HUD mode (mirrored for the windscreen) ([App Store](https://apps.apple.com/us/app/car-scanner-elm-obd2/id1259933623)).

**Data binding.**
- Only supported values are offered.
- There is a sensor "Role Override", for example mapping a custom "HV Current" PID to the EV_BatteryCurrent role so derived values work ([macheforum](https://www.macheforum.com/site/threads/tips-on-using-carscanner-elm-app-with-obd2-for-custom-data-display-ver-1-87-1-and-later.12627/)).
- One user said the docs state it "can't do calculations on multiple PID values" (same thread, 2021; may have changed, **unverified** for 2026).

**Multi-vehicle.** Connection profiles ship with prebuilt pages: the Mach-E profile brings "over 10 pages with Mach-e indicators" ([macheforum](https://www.macheforum.com/site/threads/tips-on-using-carscanner-elm-app-with-obd2-for-custom-data-display-ver-1-87-1-and-later.12627/)).

**Sharing.** Settings > Dashboard > Restore Dashboard imports a `dashboard.json`. The warning is that it "may overwrite your existing dashboards" (same thread). No in-app gallery found (**unverified**). A user asks for "more dashboard templates" and an easier way to share ([justuseapp reviews, JTalbert](https://justuseapp.com/en/app/1259933623/car-scanner-elm-obd2/reviews)).

**Screen sizes.** An iPad mini user asked for "more layouts for iPad (6, 8, 10 up)", and for gauges that scale backwards and repositionable values (justuseapp, Rmg2768). Layout templates are fixed per form factor, and users want more of them.

**Car screens.** Android Auto and CarPlay show text lists, not the custom gauges ([macheforum AA thread](https://www.macheforum.com/site/threads/car-scanner-on-android-auto.33966/), snippet only).

**Praise.**
- "The adjustable size moving graphs of parameters on the dashboard is brilliant" (Macxlab).
- "Love the amount of stuff you can change with custom dashboard gauges" (Tazaholic4life) ([justuseapp](https://justuseapp.com/en/app/1259933623/car-scanner-elm-obd2/reviews)).

**Complaints.**
- "You have to have the unit connected to customize dashboards which is not convenient" (IOS_dude, justuseapp). This contradicts the 2025 review above; it is likely older and fixed.
- "tried to create my own ... couldn't get it in a way that I liked" (bshaw, [macheforum](https://www.macheforum.com/site/threads/tips-on-using-carscanner-elm-app-with-obd2-for-custom-data-display-ver-1-87-1-and-later.12627/)).
- Wanted: "multiple parameters of similar values on a single gauge display" (Gms627).

---

## 4. OBD Fusion / OBDLink app (OCTech) and DashCommand (Palmer Performance)

### OBD Fusion and OBDLink (same engine; the help article exists on both obdsol.com and obdlink.com)

**Adding.** Three-dots menu > **Add Display** > display type > **built-in style** > data. The help advises connecting first, because otherwise the list includes PIDs the vehicle may not support. Unsupported PIDs show in **red text** and supported ones in **yellow** ([OBDLink help: Add and Edit Dashboard Gauges](https://support.obdlink.com/support/solutions/articles/43000678883)).

**Editing.** Press and hold a gauge for the Edit Display menu ([same article](https://support.obdlink.com/support/solutions/articles/43000678883)):
- Size and Location, Display Configuration (title, PID, decimals, min/max), Style.
- Bring to Front / Send to Back, Copy, Remove Display.
- **Drag and Move** and **Resize** as explicit modes, plus Reset Min/Max.
- Sizes are percentages of the dashboard.
- "Currently, the gauges must be square and the lesser of the width and height dimensions is used". Later release notes say digital gauges "can now be rectangular" ([search snippet of App Store notes](https://apps.apple.com/il/app/id650684932), snippet only).

**Styles.**
- The Style editor has Frame and other tabs: colour, label size and placement, bevel.
- Built-in templates such as "Light" can be loaded, and **saved styles are reusable** ([OBDLink help](https://support.obdlink.com/support/solutions/articles/43000678883)).
- Tacoma users: "In the 'Style' menu for a given gauge, you can save a template from the menu button" ([tacomaworld p.6](https://www.tacomaworld.com/threads/obd-fusion-tips-and-tricks.590248/page-6)).
- The desktop OBD Fusion lets you "create custom dashboards on your desktop computer and share them" with mobile ([obdsoftware.net desktop](https://www.obdsoftware.net/software/obdfusiondesktop), snippet only).

**Warnings.** On the Warnings tab, each warning is a threshold, an optional comparison and a colour (for example, green between 20 and 60 mph). There are also push-button displays that trigger actions such as "Reset Total Fuel Economy" ([OBDLink help](https://support.obdlink.com/support/solutions/articles/43000678883)).

**Pages.** Add / Remove Dashboard and Dashboard Settings (background colour). "Load Default Dashboards" replaces the whole set. Export / Import lives under Settings > Help ([OBDLink help](https://support.obdlink.com/support/solutions/articles/43000678883)). Individual dashboards can now be exported and imported (App Store notes, snippet only).

**Orientation.** Users share separate horizontal and vertical dashboards: "There should be two dashboards, one ... (horizontal) and another with all the same gauges" for a vertical device ([tundras: Export OBD Fusion Dashboard](https://www.tundras.com/threads/export-obd-fusion-dashboard.72883/)). A rotation glitch fix: "exit out of Fusion (close completely), reopen and rotate the screen" ([tacomaworld](https://www.tacomaworld.com/threads/obd-fusion-tips-and-tricks.590248/page-6)).

**Complaints.**
- "Doing this manually is taking forever!"
- "all the numbers are cluttered. Is there a way to have wider intervals?" Answer: "Intervals appear to be hard coded". Tick density is tied to range and size, with no override.
- "I keep running into this and it seems unnessecarily time consuming to delete and start over..." ([tacomaworld p.6](https://www.tacomaworld.com/threads/obd-fusion-tips-and-tricks.590248/page-6)).
- Hidden entry points: "I don't have 3 little dots?" ([tundras](https://www.tundras.com/threads/export-obd-fusion-dashboard.72883/)).
- Performance: "beyond four simultaneous gauges, the refresh rate slows" (owner reports, [obdadvisor](https://obdadvisor.com/obd-fusion-app-review/)). On ELM327 adapters, each extra PID on screen slows every PID.

**Praise.** Live-data customisation is called "Best-in-Class": you can adjust "the size, color, and even needle depth on each gauge" ([obdadvisor](https://obdadvisor.com/obd-fusion-app-review/)).

### DashCommand and the DashXL Skin Editor (Palmer Performance)

- The in-app experience is choosing skins. "Built into the application is the DashXL.net site, where you can find and select new dash skins", from "a large community of designers"; MotoIQ calls skins "the coolest feature" ([MotoIQ](https://motoiq.com/tested-palmer-performance-engineerings-dashcommand)).
- Skins are authored in a free desktop **DashXL Skin Editor**, whose help says: "**This is not a click-and-drag editor.** ... most of the positioning is done by entering the coordinates in the Dashboard Object Properties window" ([DashXL Getting Started](https://www.palmerperformance.com/products/dashxl/help/GettingStarted.html)).
  - Its panes are XY Position, an object tree with nested objects, a properties window, and a **Needle Tester** that drives gauges from a log file via a slider (same page).
  - A gauge is built from several elements (arcs, ticks, text) "to allow for maximum customizability" (same page).
- A `.dxls` skin set holds multiple views, and "each view stores the aspect ratio of the view" ([fileinfo .DXLS](https://fileinfo.com/extension/dxls)). Palmer claims dashboards "look great on any screen size" thanks to DashXL ([DashCommand Windows](https://www.palmerperformance.com/products/dashcommand/windows/)).
- Two tiers: in the car you pick and lightly configure; a power user designs on a PC. Per-aspect-ratio views are how one skin fits many screens.

---

## 5. RaceChrono Pro and Harry's LapTimer

**RaceChrono.** The live-screen and video-overlay editors grew over many versions ([Android changelog](https://racechrono.com/article/352)):
- v5.1.0: "Added editable Live timer with presets: Classic, Modern and Modern with gauges".
- v8.0.2: "A new overlay editor with much more configurability".
- v10.0.0: "Added ability to place images on video overlays".
- v10.3.0: "Added bar gauge for video overlay and live screen".

Notable fixed bugs (same changelog):
- "video overlay editor changes lost if screen rotated" (v4.1.5).
- "Wrong overlay aspect ratio for non-16:9 video frames" (v8.0.7).
- "Gauge cluster was too big on 4:3 video frame" (v6.0.4).
- "Fixed: Broken overlay gauge updates after editing or changing it" (v7.3.0).

Before v8, gauges sat in side groups: moving one shifted its whole group, and Up / Down changed only order ([search snippet of RaceChrono forum discussion 1674](https://racechrono.com/forum/discussion/1674/adding-rpm-and-throttle-position-gauge), snippet only). The "Modern with gauges" presets are a live-screen "template" pattern.

**Harry's LapTimer.** The Timer View is "the LapTimer view used while driving by most serious users". Its configurable elements are documented only in a Facebook album and an in-app Quick Reference, and availability "depends on the LapTimer Edition as well as on the platform" ([gps-laptimer.de documentation](https://gps-laptimer.de/documentation)). I could not see how the Timer View is customised: **unverified**. The trait worth noting is configuration by choosing from fixed elements and display options, not free-form layout.

---

## 6. Carista, Scan My Tesla, others

- **Carista:** live data (RPM, speed, temperatures, fuel trims) behind a Pro subscription (USD 14.99/month or 59.99/year), and on CarPlay / Android Auto. **No user dashboard builder** found ([carista.com](https://carista.com/), [App Store](https://apps.apple.com/app/id954363569)).
- **Scan My Tesla:** CAN telemetry viewer. No documentation of a layout builder found; a HUD mode is the top user request ([marlvel.ai summary](https://marlvel.ai/api/llm/apps/utilities/scanmytesla), snippet only). **Unverified** whether tabs are user-editable.
- **"Infinitas":** no automotive dash app by that name found. The OBDLink app is covered in §4 (same engine as OBD Fusion).
- **Adjacent: SimHub Dash Studio** (sim racing; a PC editor with a component list, padlock locking, a screen list with drag-to-reorder, a fixed dashboard width and height, and a zoom slider; the gauge "Value" property can be edited to preview different values) ([SimHub wiki, 2017](https://github.com/SHWotever/SimHub/wiki/Dash-Studio-Editor-Overview)).

---

## 7. Hardware dash editors: what professionals expect

| Tool | Key traits | Source |
|---|---|---|
| **MoTeC Dash Manager + Display Creator** | PC-only. Display Creator is "a virtual blank canvas": templates or from scratch, high-res images, international fonts, multiple pages. Alarms show in a warning area alongside ECU warnings. Colour-change states, e.g. the tach bezel turns orange below 25 % fuel; the background turns green when RPM and boost reach the launch target. Content overrides automatically, e.g. a door-open message replaces the gear indicator. A button cycles pages; a button swaps a gauge between boost and speed. Constants and thresholds live in Calculations / Constants. Users learn "with the simulator". The C series needs a paid enable key. | [HP Academy](https://www.hpacademy.com/blog/motec-releases-display-creator-software-custom-dash-configuration), [T1 Race Dev C127 guide](https://help.t1racedevelopment.com/support/solutions/articles/4000136020-gt1r-motec-c127-dash-replacement-with-display-creator-layout), [MoTeC forum snippet](https://forum.motec.com.au/viewtopic.php?p=11975) |
| **Haltech NSP** | Up to five screen types, added from a preconfigured list; thumbnails are dragged to reorder. Each component's outline turns cyan when selected; Select opens the channel list. Labels can be renamed (MAP vs "Vacuum/Boost"); units and decimals per component. **Day and night colour schemes**. Separate *Display* min/max and *Warning* min/max. Alarms have message, source, thresholds, **priority ordering**, "disable when engine off", and manual or timer clearing (short press clears the latest, long press clears all). LEDs are bound to alarms; shift lights get a colour and RPM per LED. | [Haltech: Navigating dashes in NSP](https://support.haltech.com/portal/en/kb/articles/navigating-dashes-in-nsp) |
| **Holley EFI Pro Dash** | **On-device editor**: a "customize" mode, then add a gauge, label or switch. Nine gauge types; each has size, range, sweep, colours, segments and orientation. Warning and alarm colours, with an optional flash. Custom backgrounds from USB, with the bezel baked into the image and only the pointer live. **Hard cap of 32 active gauges**. A USB mouse and keyboard work. Multiple stored layouts; swipe between screens. Wired buttons switch layouts. | [Race Engine Technology](https://www.raceenginetechnology.com/News/holleys-pro-dash-offers-endless-customization-options), [Holley 553-111](https://www.holley.com/products/gauges_and_gauge_accessories/parts/553-111) |
| **AiM Race Studio 3** (also Link ECU's Strada dashes) | The device configuration's "Display" tab: choose among page layouts, then assign channels and end-of-scale values. A "wide library of page styles". Icon management with custom icon upload. RGB alarm LEDs (solid or flashing) with text messages. **Math channels**. Import / export of the whole configuration. Link's MXS/MXG/MXT dashes are configured through PCLink plus RS3, and the MXG navigates a "library of preset display styles". | [AiM FAQ RS3 display customisation](https://store.aimsports.com/download/faqs/eng/software/rs3/FAQ_RS3_DisplayCustomization_100_eng.pdf), [Link dash overview](https://linkecu.com/dash-display-overview/), [aimshop MXS icons](https://aimshop.com/products/aim-mxs-1-2-strada-dash-display-race-icons) |
| **Ecumaster ADU Manager** | A PC page editor with a **live preview sent to the connected device in real time**, and a **test value typed into a channel** to check an indicator ("Testing a control", p.43). Page types: standard, overlay, overlay with background. Overlays appear on a channel or function ("activation channel"). Buttons switch only standard pages. Logic Functions, Tables, Switches, Timers. Alarms have a minimum show time; users build their own acknowledge logic. Rich objects (gauges, bar graphs, G-force, tyre temperature, track tables, shapes, textures). Per-object custom units. | [ManualsLib ADU-5 TOC](https://www.manualslib.com/manual/1521241/Ecumaster-Adu-5.html), [Ecumaster community](https://community.ecumaster.com/t/questions-about-adu-configuration/4211), [Momentary data display](https://community.ecumaster.com/t/momentary-data-display/1779) |

What all the pro tools share:
1. Separate *display range* and *warning thresholds*.
2. Alarms as first-class objects with priority, conditions (engine running) and acknowledge rules.
3. Pages or overlays switched by buttons **and by conditions** (alarm, mode, pit limiter, door open).
4. Day and night colour schemes.
5. A simulator or test-value preview.
6. Math channels.
7. Whole-configuration import / export.
8. Desktop-first authoring. Holley is the main exception with an on-device editor, and even it supports a mouse.

---

## 8. Cross-app patterns

- **Template or layout first beats a blank canvas for most users.** Car Scanner (choose a layout, then tick values), Haltech (a preconfigured screen list), AiM (a library of page styles), RaceChrono (Classic / Modern presets) and DashCommand (a skin gallery) all start from a skeleton. RealDash also starts users from an existing dash, but its gauges are raw primitive stacks.
- **Two-tier editing.** The deep editor lives on a PC (RealDash's recommendation, DashXL, MoTeC, AiM, Ecumaster, SimHub, OBD Fusion desktop); the in-car editor is pick, swap and size.
- **Long-press as the edit entry** (Torque, OBDLink / Fusion) hides features: "I don't have 3 little dots?" and hunting through menus for delete.
- **Explicit move and resize modes** (OBDLink "Drag and Move" / "Resize") instead of always-on drag. This reduces accidental moves on touch.
- **Supported-PID awareness** (Car Scanner shows only supported values; OBDLink colours red / yellow) is valued and cheap.
- **Screen and aspect portability is the most-cited structural failure:** RealDash's locked aspect ratio, Torque's DPI-dependent layouts, Car Scanner users asking for iPad layouts, and Fusion users keeping separate horizontal and vertical dashboards. DashXL's answer is a view per aspect ratio.
- **Lost work** comes from destructive structural edits (Torque's page count), import that overwrites everything (Car Scanner, Fusion's "Load Default Dashboards"), save bugs (RealDash 2.0.2), rotation (RaceChrono), and editing at the wrong aspect ratio (RealDash).
- **Reusable styles** (OBDLink saved styles, Torque themes, RealDash per-level colours) beat repeating per-gauge edits: "Doing this manually is taking forever!"
- **Car-screen projection** (CarPlay / Android Auto) generally cannot show custom gauges: text only in Car Scanner and Fusion.

---

## Top 15 lessons for OCTAVE's editor (ranked by impact)

1. **Make layouts survive any screen, and say what happens.** This is the most-cited structural complaint: RealDash's "no way around this" on aspect ratio, Torque's DPI layouts, iPad users asking for more layouts. OCTAVE's grid is already resolution-independent. Next steps:
   - Allow an optional portrait variant per dashboard (DashXL's view per aspect ratio; Fusion users already keep two copies).
   - Preview the dashboard in the editor at the other aspect ratio or a phone size.
   - Never silently re-flow a saved spec when it is opened on a different screen.

2. **Design for touch as the primary editor, and show it is not "too clumsy".** RealDash's developer concedes touch editing is unusable; OCTAVE's users edit on a head unit. Keep:
   - grid snapping;
   - large grips (at least 48 dp at screenAutoScale 0.5);
   - a selection outline;
   - drag thresholds that rule out accidental moves.

   Consider explicit move / resize states (OBDLink) or a lock toggle (RealDash's freeze, SimHub's padlock).

3. **Add undo / redo, and keep the drafts.** RealDash has Ctrl+Z only on desktop, and the developer's answer to confusion is "make backups". Users complain about having to "delete and start over". A visible undo button, with a per-session history of spec snapshots, is cheap given OCTAVE's JSON spec. Keep the existing draft auto-stash.

4. **Never destroy a layout through a structural or import action.** Torque loses gauges when the page count changes; Car Scanner and Fusion imports overwrite the set. Rules:
   - Changing the grid size must re-fit or flag cells, never drop them.
   - Import always adds a new dashboard (OCTAVE already regenerates ids; keep that).
   - Before a destructive action, auto-save a backup version (e.g. keep the last N saves per dashboard).

5. **Start from templates, not a blank canvas.** Successful consumer flows begin with a layout skeleton (Car Scanner, AiM, Haltech, RaceChrono presets). A "New" flow could offer: Blank, Duplicate a preset, or a small set of starter layouts for each of 1, 2, 4 and 6 big gauges, plus strips. Car Scanner users explicitly ask for "more dashboard templates".

6. **Make widgets semantic units, not primitive stacks.** RealDash's need to select and rename the arc, text, value and unit separately is its core usability failure, and the reason a third party built an AI editor. OCTAVE's catalog-driven widgets with `editableProps` are the right model. Keep composites (user-primitives TODO) as single selectable objects, with an opt-in "advanced" view.

7. **Separate display range from warning and critical bands, edited per state.** Every pro tool (Haltech, Holley, MoTeC) and RealDash's Normal / Warning / Critical levels do this, and OBDLink / Car Scanner add colour and tone. Avoid RealDash's "Editing Level ALL deletes colours" trap: show a per-state preview chip and never wipe per-state values silently. Consider an optional flash and an alert sound.

8. **Bind data through a picker aware of the vehicle's supported PIDs.** OCTAVE's picker (searchable, grouped, supported first) already matches the best practice: Car Scanner hides unsupported values, OBDLink colours them. Add:
   - live current values in the picker rows when connected;
   - a clear "not supported on this car" badge;
   - recently used PIDs.

   Users praise Car Scanner because it "doesn't need to be connected to build", so keep the demo sweep for offline editing.

9. **Edit against live or simulated data, with a value scrubber.** DashXL's Needle Tester, Ecumaster's test value, SimHub's Value property and MoTeC's simulator are how designers check needles, bands and overflow. OCTAVE has the sim and a demo sweep; expose a per-widget "test value" slider that sweeps through the warning and critical bands. That catches the overlapping-text bug class RealDash users hit.

10. **Show what a dashboard costs in polling rate.** Fusion owners see refresh slow "beyond four simultaneous gauges"; Holley hard-caps at 32. OCTAVE's demand-driven polling could show an estimated update rate ("~4 Hz per value with 9 PIDs") in the editor, warning before a layout becomes sluggish on ELM327.

11. **Offer reusable styles and theme-following defaults.** "Doing this manually is taking forever!" (Fusion); OBDLink's saved styles and Torque's theme files fix it. Two pieces:
   - "Copy style / paste style" and "apply to all gauges of this type".
   - Theme-token defaults (OCTAVE's current approach), so most users never touch colours.
   - Plus day/night awareness (Haltech).

12. **Make every editor control visible and labelled; don't hide them behind long-press or a "⋯" menu.** Hidden entry points drive confusion in Torque, Fusion and RealDash. Plus: the RealDash side panel covers the dash, and its edit button vanishes in fullscreen. Put persistent, labelled actions on a toolbar that never covers the canvas, or slides away, and keep long-press as a shortcut only.

13. **Treat sharing as a feature: a single file, validation, and later a gallery.** RealDash's gallery and DashCommand's in-app skin store are major draws, while RealDash's cross-platform purchase locks and Android scoped-storage import friction are complaints. OCTAVE's sanitised, data-only JSON import / export is the right base. Next:
   - QR / share-sheet export;
   - a preview thumbnail inside the file;
   - a clear report of PIDs or widgets the importing car or build lacks (RealDash's "Input Mapping" idea, which remaps unknown inputs on import).

14. **Plan for conditional content: alarms, overlays and auto page switching.** Pros expect alarms with priority, "only when the engine is running", acknowledgement, and overlays or pages that appear on a condition (Ecumaster overlays, MoTeC door-open override, Haltech alarm priority). OCTAVE's shift light shows the pattern. A dashboard-level "alert overlay" and a "switch to page X when Y" rule would serve towing and track use without a full logic editor.

15. **Offer a desktop / browser editing path to the same spec, without making it required.** Power users everywhere prefer authoring on a PC (RealDash, DashXL, MoTeC, AiM, Ecumaster, OBD Fusion desktop). OCTAVE already has two routes: the same QML editor runs on desktop and in the WASM demo, and the service API serves `dashboardManager`. Document "design on your laptop, push to the car" (a pairing code plus save). Keep the in-car editor complete enough that no user is forced to a PC.

Honourable mentions:
- Math or derived channels (RealDash `conversion=`, AiM math channels, Torque equations) are expected by enthusiasts; Car Scanner users complain when they are missing.
- Gauge tick density must be overridable ("intervals appear to be hard coded", Fusion).
- Allow several series per gauge, as Car Scanner users request.
- CarPlay / Android Auto projection cannot show custom gauges in any competitor, so this is not a gap for OCTAVE.
