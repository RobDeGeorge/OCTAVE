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
constexpr int kProtocolVersion = 1;
}

#endif // OCTAVE_SERVICE_PROTOCOL_H
