#ifndef OCTAVE_WASM_WEBSERVICE_H
#define OCTAVE_WASM_WEBSERVICE_H

// The service protocol inside the page (WebAssembly build only): the
// frontend playground (packaging/wasm/playground.html) runs a web frontend
// against this build's real managers, with no network and no socket.
//
// A browser page can't listen on a port, so the page reaches the service
// through a JS object instead, Module.octaveService:
//
//   const id = Module.octaveService.open(text => ...)   // a connection; replies and events arrive as JSON text
//   Module.octaveService.send(id, '{"op":"hello","id":1}')
//   Module.octaveService.close(id)
//
// Each message is one service request (docs/api/protocol.json), handled by
// the same CommandServer ops and ServiceBridge a socket's line goes to
// (CommandServer::handleLineOn), on the GUI thread. A connection counts as
// a program on the device (loopback), as the page and the app are the
// visitor's own. Events (subscriptions) are collected every frame and
// handed to the connection's callback; binary stream records are not
// carried (no video in the browser build).
//
// The playground puts a tiny WebSocket stand-in into the frontend's iframe
// that speaks to this object, so an unmodified web frontend (one that
// opens new WebSocket("ws://" + location.host + "/")) runs as it would on
// the head unit.

namespace OctaveService {
class CommandServer;
}

namespace WebService {
// Installs Module.octaveService for `server` (not started: no socket in
// the browser). One per page
void install(OctaveService::CommandServer *server);
}

#endif // OCTAVE_WASM_WEBSERVICE_H
