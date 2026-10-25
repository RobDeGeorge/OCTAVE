// protocol.h
//
// The version of the service protocol (src/service/), reported by `hello`
// and `describe` and written into docs/api/. Clients pin it.
//
// Bump it on any breaking change: a removed or renamed op, object, property,
// method or signal, or a changed type or meaning. Additions (a new op, a
// new property, a new field in a reply) do not bump it; clients ignore
// fields they don't know.

#ifndef OCTAVE_SERVICE_PROTOCOL_H
#define OCTAVE_SERVICE_PROTOCOL_H

namespace OctaveService {
// 2 (2026-10-23): pairing. Over the network every op but hello and pair
// needs a paired device's token; loopback stays open by default, where a
// v1 client sees no difference except protocolVersion 2.
// 3 (2026-10-24): the music downloader is gone, and with it the
// downloadManager object.
constexpr int kProtocolVersion = 3;
}

#endif // OCTAVE_SERVICE_PROTOCOL_H
