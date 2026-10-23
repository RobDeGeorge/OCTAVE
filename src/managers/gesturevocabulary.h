// gesturevocabulary.h
//
// The gesture sensor's vocabulary in one place: the gestures the PAJ7620U2
// worker can report, the actions a gesture can trigger (main.cpp dispatches
// them; "none" does nothing), and the default gesture -> action mapping
// (SettingsManager's default gestureMapping and GestureManager's fallback).
// Header-only so SettingsManager can use it without linking the sensor code.

#ifndef GESTUREVOCABULARY_H
#define GESTUREVOCABULARY_H

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace GestureVocabulary {

// In the order the settings page lists them
inline QStringList gestures()
{
    return {QStringLiteral("RIGHT"), QStringLiteral("LEFT"), QStringLiteral("UP"), QStringLiteral("DOWN"),
            QStringLiteral("FORWARD"), QStringLiteral("BACKWARD"), QStringLiteral("CLOCKWISE"),
            QStringLiteral("COUNTER-CLOCKWISE"), QStringLiteral("WAVE")};
}

// {key, label} per action, "none" (Disabled) last
inline QVariantList actions()
{
    const QList<QPair<QString, QString>> rows{
        {QStringLiteral("next_track"), QStringLiteral("Next Track")},
        {QStringLiteral("previous_track"), QStringLiteral("Previous Track")},
        {QStringLiteral("volume_up"), QStringLiteral("Volume Up")},
        {QStringLiteral("volume_down"), QStringLiteral("Volume Down")},
        {QStringLiteral("play_pause"), QStringLiteral("Play / Pause")},
        {QStringLiteral("mute_toggle"), QStringLiteral("Mute Toggle")},
        {QStringLiteral("none"), QStringLiteral("Disabled")},
    };
    QVariantList out;
    for (const auto &r : rows)
        out.append(QVariantMap{{QStringLiteral("key"), r.first}, {QStringLiteral("label"), r.second}});
    return out;
}

inline QVariantMap defaultMapping()
{
    return {{QStringLiteral("RIGHT"), QStringLiteral("next_track")},
            {QStringLiteral("LEFT"), QStringLiteral("previous_track")},
            {QStringLiteral("UP"), QStringLiteral("volume_up")},
            {QStringLiteral("DOWN"), QStringLiteral("volume_down")},
            {QStringLiteral("FORWARD"), QStringLiteral("play_pause")},
            {QStringLiteral("BACKWARD"), QStringLiteral("mute_toggle")},
            {QStringLiteral("CLOCKWISE"), QStringLiteral("volume_up")},
            {QStringLiteral("COUNTER-CLOCKWISE"), QStringLiteral("volume_down")},
            {QStringLiteral("WAVE"), QStringLiteral("play_pause")}};
}

} // namespace GestureVocabulary

#endif // GESTUREVOCABULARY_H
