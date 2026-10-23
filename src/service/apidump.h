// apidump.h
//
// The published API (TODO/open-backend.md, contract step 3): what the
// service serves, written as files a frontend author reads before writing a
// line. `octave --dump-api DIR` builds the managers with no UI and calls
// this; docs/api/ in the repo is its output for the desktop build, and
// tst_apidocs fails when that copy is stale.
//
//   index.json         {protocolVersion, objects: {name: class}}
//   <name>.json        one object's describe output (metadescribe.h)
//   parameters.json    the PID / IMU catalog (parameterCatalog.parameters)
//
// The files are pretty-printed JSON with sorted keys, so a regeneration
// diffs cleanly. Hand-written files in the same directory (the README, the
// dashboard spec schema) are left alone.

#ifndef OCTAVE_SERVICE_APIDUMP_H
#define OCTAVE_SERVICE_APIDUMP_H

#include <QDir>
#include <QString>

namespace OctaveService {

class ServiceRegistry;

// False with `error` set if a file could not be written
bool writeApiDocs(const ServiceRegistry &registry, const QDir &dir, QString *error);

} // namespace OctaveService

#endif // OCTAVE_SERVICE_APIDUMP_H
