// units.h
//
// Display formatting and unit conversion in one place: play / track times,
// compass headings, temperature, length and speed. The free functions serve
// C++ (MediaManager's library total); the Units QObject (context property
// "units") gives QML the same rules, so no page carries its own copy.

#ifndef UNITS_H
#define UNITS_H

#include <QObject>
#include <QString>

#include <cmath>

namespace UnitsFn {
// "m:ss", or "h:mm:ss" from one hour; negative, NaN or infinite -> "0:00"
QString formatDuration(double ms);
// 8-point compass label for a heading in degrees (N at 0, NE at 45, ...)
QString cardinal(double headingDeg);
inline double cToF(double c) { return c * 9.0 / 5.0 + 32.0; }
inline double fToC(double f) { return (f - 32.0) * 5.0 / 9.0; }
inline double mToFt(double m) { return m * 3.28084; }
inline double mphToKph(double mph) { return mph * 1.609344; }
inline double kphToMph(double kph) { return kph / 1.609344; }
// Amplitude ratio <-> decibels (20 log10); silence reads -120 dB
inline double linearToDb(double x) { return x > 0 ? 20.0 * std::log10(x) : -120.0; }
inline double dbToLinear(double db) { return std::pow(10.0, db / 20.0); }
} // namespace UnitsFn

class Units : public QObject
{
    Q_OBJECT
public:
    explicit Units(QObject *parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE QString formatDuration(double ms) const { return UnitsFn::formatDuration(ms); }
    Q_INVOKABLE QString formatSeconds(double s) const { return UnitsFn::formatDuration(s * 1000.0); }
    Q_INVOKABLE QString cardinal(double headingDeg) const { return UnitsFn::cardinal(headingDeg); }
    Q_INVOKABLE double cToF(double c) const { return UnitsFn::cToF(c); }
    Q_INVOKABLE double mToFt(double m) const { return UnitsFn::mToFt(m); }
    Q_INVOKABLE double mphToKph(double mph) const { return UnitsFn::mphToKph(mph); }
    Q_INVOKABLE double linearToDb(double x) const { return UnitsFn::linearToDb(x); }
    Q_INVOKABLE double dbToLinear(double db) const { return UnitsFn::dbToLinear(db); }
};

#endif // UNITS_H
