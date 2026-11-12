#pragma once

#include <QString>
#include <QStringList>

// The licence texts compiled into the binary (CMakeLists.txt,
// octave_add_license_texts) for Settings > About > Open-Source Licenses:
// OCTAVE's own LICENSE and NOTICE, then one text per licence of a bundled
// component (licenses/*.txt).
namespace LicenseTexts {

// "LICENSE", "NOTICE", "LGPL-3.0", ... in display order
QStringList names();

// The full text; "" for a name that names() does not hold
QString text(const QString &name);

} // namespace LicenseTexts
