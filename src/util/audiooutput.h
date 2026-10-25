// audiooutput.h
//
// Which audio output OCTAVE plays to: the device the user picked
// (settingsManager.audioOutputDevice, a QAudioDevice id) while it is
// present, otherwise the system default. MediaManager (local music) and
// ScrcpyClient (phone audio) both ask here, so they always agree.

#ifndef AUDIOOUTPUT_H
#define AUDIOOUTPUT_H

#include <QAudioDevice>
#include <QMediaDevices>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace AudioOutput {
// The picked device when it is plugged in; else the system default (null
// when there is no output at all). An empty id means "system default".
inline QAudioDevice preferred(const QString &id)
{
    if (!id.isEmpty()) {
        const QList<QAudioDevice> outputs = QMediaDevices::audioOutputs();
        for (const QAudioDevice &d : outputs) {
            if (QString::fromUtf8(d.id()) == id)
                return d;
        }
    }
    return QMediaDevices::defaultAudioOutput();
}

// Every output as {id, name, isDefault}, for a picker
inline QVariantList list()
{
    QVariantList out;
    const QList<QAudioDevice> outputs = QMediaDevices::audioOutputs();
    for (const QAudioDevice &d : outputs) {
        out.append(QVariantMap{{QStringLiteral("id"), QString::fromUtf8(d.id())},
                               {QStringLiteral("name"), d.description()},
                               {QStringLiteral("isDefault"), d.isDefault()}});
    }
    return out;
}
} // namespace AudioOutput

#endif // AUDIOOUTPUT_H
