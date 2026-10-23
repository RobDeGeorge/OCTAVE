#ifndef DTCDESCRIPTIONS_H
#define DTCDESCRIPTIONS_H

// Plain-language descriptions of OBD-II trouble codes, for every frontend
// (obdManager's dtcCodes and freeze frame `description`). The ECU gives
// only the code. Two levels:
//   - a table of common generic (SAE J2012) codes with their meaning;
//   - for any other code, what its structure says for certain: the system
//     (P powertrain, C chassis, B body, U network), generic or
//     manufacturer-specific, and for powertrain the subsystem from the
//     third character ("Generic powertrain code: ignition system or
//     misfire"). Never a guessed specific meaning.

#include <QString>

namespace DtcDescriptions {

// "" only for something that is not a DTC (not a letter and four hex digits)
QString describe(const QString &code);

// Whether `code` has a specific entry (not only its category)
bool isKnown(const QString &code);

} // namespace DtcDescriptions

#endif // DTCDESCRIPTIONS_H
