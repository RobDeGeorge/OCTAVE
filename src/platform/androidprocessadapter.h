#ifndef ANDROIDPROCESSADAPTER_H
#define ANDROIDPROCESSADAPTER_H

// QProcess-shaped wrapper that runs yt-dlp / ffmpeg via the JNI bridge
// (OctaveDownloadBridge) on Android. Download-only, so it compiles out with
// OCTAVE_ENABLE_DOWNLOADS=OFF like the Java class it calls. Mirrors enough of QProcess's API that
// downloadmanager.cpp can use it interchangeably through a conditional typedef.
//
// Key difference from QProcess: no stdin; no live streaming of stdout.
// The JNI call blocks the worker thread until done, then the whole combined
// output string is delivered via readyReadStandardOutput + stored in the
// stdout buffer. That's sufficient for both search (JSON dump) and download
// (log-line parsing post-hoc).

// Guard on Q_OS_MOBILE (not Q_OS_ANDROID) because moc only sees defines
// emitted into moc_predefs.h — and our CMakeLists sets Q_OS_MOBILE there.
#if defined(Q_OS_MOBILE) && defined(OCTAVE_ENABLE_DOWNLOADS)

#include <QObject>
#include <QProcess>   // for QProcess::ExitStatus and ProcessError enums
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QThread>
#include <QMutex>
#include <QMutexLocker>
#include <atomic>

#include "androidmediabridge.h"

class AndroidProcessAdapter : public QObject
{
    Q_OBJECT

public:
    enum Tool { YtDlp, FFmpeg };

    explicit AndroidProcessAdapter(QObject *parent = nullptr, Tool tool = YtDlp)
        : QObject(parent), m_tool(tool) {}

    ~AndroidProcessAdapter() override
    {
        if (m_worker && m_worker->isRunning()) {
            m_canceled.store(true);
            m_worker->wait(2000);
            m_worker->deleteLater();
        }
    }

    // QProcess-shaped API
    void setProcessChannelMode(QProcess::ProcessChannelMode) {}

    // Mirrors QProcess::start(program, args) — program arg is IGNORED;
    // the constructor's `tool` decides what runs. Args are passed verbatim
    // to the JNI bridge.
    void start(const QString & /*program*/, const QStringList &arguments)
    {
        if (m_worker && m_worker->isRunning())
            return;

        m_canceled.store(false);
        m_stdout.clear();
        m_stderr.clear();

        m_worker = QThread::create([this, arguments] {
            // FFmpeg is executed directly via QProcess elsewhere (we get the
            // binary path via JNI and spawn it normally); this adapter is
            // currently yt-dlp-only. m_tool kept for future extensibility.
            Q_UNUSED(m_tool);
            QString out = OctaveAndroid::runYtDlp(arguments);

            int rc = OctaveAndroid::lastExitCode();
            {
                QMutexLocker locker(&m_bufferMutex);
                m_stdout = out.toUtf8();
            }

            if (m_canceled.load()) {
                // Canceled — don't emit finished
                return;
            }

            emit readyReadStandardOutput();
            QProcess::ExitStatus st = (rc == 0) ? QProcess::NormalExit : QProcess::CrashExit;
            emit finished(rc, st);
        });
        m_worker->setObjectName(m_tool == YtDlp ? "OCTAVE-ytdlp" : "OCTAVE-ffmpeg");
        m_worker->start();
    }

    void kill()
    {
        // The JNI bridge doesn't expose a cancellation hook — best we can do
        // is flag the thread so it suppresses its final emit and let it finish
        // naturally. For search/download this means the process runs to completion
        // in the background but the UI treats it as canceled. Acceptable for MVP.
        m_canceled.store(true);
        emit errorOccurred(QProcess::Crashed);
    }

    bool waitForFinished(int msec = 30000)
    {
        if (!m_worker)
            return true;
        return m_worker->wait(msec);
    }

    QByteArray readAllStandardOutput()
    {
        QMutexLocker locker(&m_bufferMutex);
        QByteArray out = m_stdout;
        m_stdout.clear();
        return out;
    }

    // Alias used by some call sites (QIODevice-style).
    QByteArray readAll()
    {
        return readAllStandardOutput();
    }

    // No real process ID behind this — the JNI bridge runs on a Qt thread.
    // Returning 0 makes pause/resume SIGSTOP/SIGCONT paths no-op (which is
    // correct: we can't pause yt-dlp mid-download through the JNI API anyway).
    qint64 processId() const { return 0; }

    QByteArray readAllStandardError()
    {
        QMutexLocker locker(&m_bufferMutex);
        QByteArray out = m_stderr;
        m_stderr.clear();
        return out;
    }

signals:
    void finished(int exitCode, QProcess::ExitStatus status);
    void errorOccurred(QProcess::ProcessError error);
    void readyReadStandardOutput();
    void readyReadStandardError();

private:
    Tool m_tool;
    QThread *m_worker = nullptr;
    QByteArray m_stdout;
    QByteArray m_stderr;
    QMutex m_bufferMutex;
    std::atomic<bool> m_canceled{false};
};

#endif // Q_OS_MOBILE && OCTAVE_ENABLE_DOWNLOADS
#endif // ANDROIDPROCESSADAPTER_H
