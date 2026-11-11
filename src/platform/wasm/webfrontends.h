#ifndef OCTAVE_WASM_WEBFRONTENDS_H
#define OCTAVE_WASM_WEBFRONTENDS_H

// The web frontends the site offers, as installed frontends (WebAssembly
// build only). The page (packaging/wasm/index.html) reads the site's
// frontends/catalog.json (packaging/wasm/stage_frontends.py: each one's own
// octave-frontend.json, plus where it is on the site) into
// globalThis.octaveFrontendCatalog before the core starts. install() puts a
// folder for each into the frontends root in the page's memory, so
// Settings > Device > Frontends lists them as on a head unit, and Use is
// FrontendManager::setActive as everywhere.
//
// The files themselves stay on the site: the page follows
// frontendManager.activeId over its own service connection and shows the
// chosen one over this screen in a sandboxed iframe, connected to this core
// (packaging/wasm/frontend-host.js); its OCTAVE button calls setActive("").
// There is no restart in a page, so the choice applies at once, and mute
// pressed five times (VolumeController::escapeGesture) brings this screen
// back here too.

class FrontendManager;
class VolumeController;

namespace WebFrontends {
void install(FrontendManager *frontends, VolumeController *volume);
}

#endif // OCTAVE_WASM_WEBFRONTENDS_H
