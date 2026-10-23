// servicescopes.h
//
// The permissions a paired device can hold (servicepairing.h), and what
// each member needs (Q_CLASSINFO("octave.scope...") on the class, see
// metadescribe.h). Every paired device can read: get, subscribe, rows,
// describe, /art and the streams that aren't the phone's screen. Changing
// anything needs the member's scope, or `full`. The device's own screen
// (loopback, while serviceLoopbackOpen is on) has full.
//
//   read      nothing that changes state
//   media     playback, volume, the queue, the visualiser
//   vehicle   OBD, adapters, the IMU, gestures, the shift light, the knob
//   phone     phone mirroring and Android Auto, their screens and input
//   settings  settingsManager and the themes
//   files     the library, playlists, downloads, dashboards, trips, logs:
//             what is saved, deleted, imported or exported
//   system    restart, shut down, update, open folders on the head unit
//   full      all of the above, the dev actions and app internals
// Header-only, for every target that links the command server.

#ifndef OCTAVE_SERVICE_SERVICESCOPES_H
#define OCTAVE_SERVICE_SERVICESCOPES_H

#include <QStringList>

namespace OctaveService {

inline const QStringList &knownScopes()
{
    static const QStringList scopes{QStringLiteral("read"),     QStringLiteral("media"),    QStringLiteral("vehicle"),
                                    QStringLiteral("phone"),    QStringLiteral("settings"), QStringLiteral("files"),
                                    QStringLiteral("system"),   QStringLiteral("full")};
    return scopes;
}

// Whether a device holding `held` may do what needs `needed`
inline bool scopeAllows(const QStringList &held, const QString &needed)
{
    return needed == QLatin1String("read") || held.contains(QLatin1String("full")) || held.contains(needed);
}

} // namespace OctaveService

#endif // OCTAVE_SERVICE_SERVICESCOPES_H
