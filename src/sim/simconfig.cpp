#include "simconfig.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QtEndian>

#include <algorithm>
#include <cmath>

namespace OctaveSim {

namespace {
constexpr int kRate = 22050;
constexpr double kPi = 3.14159265358979323846;

void putLE32(QByteArray &b, quint32 v)
{
    char c[4];
    qToLittleEndian(v, c);
    b.append(c, 4);
}

void putLE16(QByteArray &b, quint16 v)
{
    char c[2];
    qToLittleEndian(v, c);
    b.append(c, 2);
}

// One INFO sub-chunk: id, size, NUL-terminated text padded to even length
void putInfo(QByteArray &b, const char *id, const QString &text)
{
    QByteArray t = text.toUtf8() + '\0';
    if (t.size() % 2)
        t += '\0';
    b.append(id, 4);
    putLE32(b, quint32(t.size()));
    b += t;
}

// An arpeggio over `notes` (Hz), `seconds` long, each note with a soft
// attack and decay so it is pleasant enough to leave on
QByteArray tone(const QList<double> &notes, double noteSeconds, int seconds)
{
    QByteArray pcm;
    const int total = kRate * seconds;
    const int perNote = int(kRate * noteSeconds);
    pcm.reserve(total * 2);
    for (int i = 0; i < total; ++i) {
        const double f = notes.at((i / perNote) % notes.size());
        const double t = double(i % perNote) / kRate;
        const double env = std::min(1.0, t * 40.0) * std::exp(-3.0 * t);
        const double s = env * (0.6 * std::sin(2 * kPi * f * t) + 0.2 * std::sin(4 * kPi * f * t));
        putLE16(pcm, quint16(qint16(s * 0.5 * 32767)));
    }
    return pcm;
}

QByteArray wav(const QByteArray &pcm, const QString &title, const QString &artist, const QString &album, int track)
{
    QByteArray info("INFO", 4);
    putInfo(info, "INAM", title);
    putInfo(info, "IART", artist);
    putInfo(info, "IPRD", album);
    putInfo(info, "ITRK", QString::number(track));
    QByteArray body("WAVE", 4);
    body += "fmt ";
    putLE32(body, 16);
    putLE16(body, 1);           // PCM
    putLE16(body, 1);           // mono
    putLE32(body, kRate);
    putLE32(body, kRate * 2);   // bytes a second
    putLE16(body, 2);           // block align
    putLE16(body, 16);          // bits
    body += "LIST";
    putLE32(body, quint32(info.size()));
    body += info;
    body += "data";
    putLE32(body, quint32(pcm.size()));
    body += pcm;
    QByteArray out("RIFF", 4);
    putLE32(out, quint32(body.size()));
    return out + body;
}
} // namespace

QStringList writeTestTracks(const QString &dir)
{
    struct Track {
        const char *title;
        QList<double> notes;
        double noteSeconds;
    };
    const QList<Track> tracks{
        {"Idle", {220.0, 277.18, 329.63, 440.0}, 0.5},
        {"Cruise", {261.63, 329.63, 392.0, 523.25, 392.0, 329.63}, 0.35},
        {"Redline", {329.63, 415.3, 493.88, 659.25}, 0.18},
    };
    QDir().mkpath(dir);
    QStringList written;
    for (int i = 0; i < tracks.size(); ++i) {
        const Track &t = tracks.at(i);
        const QString name = QStringLiteral("%1 - %2.wav").arg(i + 1, 2, 10, QLatin1Char('0')).arg(QLatin1String(t.title));
        QFile f(QDir(dir).filePath(name));
        if (!f.open(QIODevice::WriteOnly))
            continue;
        f.write(wav(tone(t.notes, t.noteSeconds, 20), QLatin1String(t.title), QStringLiteral("OCTAVE Simulator"),
                    QStringLiteral("Test Tones"), i + 1));
        written.append(name);
    }
    return written;
}

bool seedThrowawayConfig(const QString &configDir, const QString &musicDir)
{
    QDir().mkpath(configDir);
    const QJsonObject settings{
        {QStringLiteral("mediaFolder"), musicDir},
        // The head unit's own hardware is not part of the simulation
        {QStringLiteral("esp32VolumeEnabled"), false},
        {QStringLiteral("gestureSensorEnabled"), false},
    };
    QFile f(QDir(configDir).filePath(QStringLiteral("settingsConfigure.json")));
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(settings).toJson());
    f.close();
    return writeTestTracks(musicDir).size() == 3;
}

} // namespace OctaveSim
