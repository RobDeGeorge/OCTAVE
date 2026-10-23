#ifndef FRONTENDSUPERVISOR_H
#define FRONTENDSUPERVISOR_H

// Runs the active exec frontend (frontendmanager.h) while OCTAVE is
// headless: the frontend is the screen, OCTAVE the backend it talks to.
//
// Each launch gets a fresh session token (ServicePairing::addSessionDevice)
// holding exactly the manifest's scopes; the previous one is revoked. The
// program finds the service from its environment:
//   OCTAVE_HOST=127.0.0.1  OCTAVE_PORT  OCTAVE_TOKEN  OCTAVE_PROTOCOL
//   OCTAVE_SCOPES (comma separated)  OCTAVE_FRONTEND_DIR
// and runs in its own folder; its output goes to OCTAVE's log. main.cpp
// turns loopback trust off for the run, so the token is the only way in.
//
//   - exit code 0: the frontend quit on purpose, and OCTAVE quits with it
//     (exitedCleanly). Except within quickQuitMs of starting: a frontend
//     that quits at once on every start would leave a blank head unit, so
//     that counts as a crash;
//   - anything else (a crash, a non-zero code, failing to start): started
//     again after a back-off (1 s, doubling to 30 s). A run longer than a
//     minute forgives the crashes before it;
//   - kMaxQuickCrashes crashes in a row, each within a minute of starting:
//     gaveUp(reason), and main.cpp writes USE_QML and starts OCTAVE again
//     on the QML UI.
// Its output is relayed to the log as "[id] line", at most
// logLinesPerSecond lines and logBytesPerSecond bytes a second, each line
// cut at logLineMax characters; what is over goes, with one "N lines
// dropped" note a second, so a frontend that prints megabytes cannot keep
// the logger busy.
// stop() asks the program to quit (SIGTERM on Unix), and kills it after
// stopGraceMs. Needs QProcess (QT_CONFIG(process)); without it start()
// gives up at once.

#include "frontendmanifest.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>

class QProcess;
class ServicePairing;

class FrontendSupervisor : public QObject
{
    Q_OBJECT

public:
    struct Options {
        int maxQuickCrashes = 5;
        int quickRunMs = 60000;
        int firstBackoffMs = 1000;
        int maxBackoffMs = 30000;
        int stopGraceMs = 3000;
        int quickQuitMs = 3000;
        int logLinesPerSecond = 200;
        int logBytesPerSecond = 64 * 1024;
        int logLineMax = 1000;
    };

    FrontendSupervisor(const FrontendManifest &manifest, const QString &dir, ServicePairing *pairing, quint16 port,
                       QObject *parent = nullptr);
    ~FrontendSupervisor() override;

    void setOptions(const Options &options) { m_options = options; }
    void start();
    void stop();

    bool running() const;
    int launches() const { return m_launches; }
    qint64 relayedLines() const { return m_relayed; }
    qint64 droppedLines() const { return m_droppedTotal; }
    // argv as it is started: the program resolved (inside the folder, or
    // on PATH) and {dir} replaced; empty with *error set when it can't be
    static QStringList resolveCommand(const FrontendManifest &manifest, const QString &dir, QString *error);

signals:
    void started(qint64 pid);
    void exitedCleanly();
    void gaveUp(const QString &reason);

private:
    void launch();
    void finished(int code, bool crashed, const QString &what);
    void revokeToken();
    void relayOutput();
    void relayLine(const QByteArray &line, bool cut);
    void noteDropped();

    FrontendManifest m_manifest;
    QString m_dir;
    QPointer<ServicePairing> m_pairing;
    quint16 m_port;
    Options m_options;
    QProcess *m_process = nullptr;
    QTimer m_restart;
    QElapsedTimer m_runTime;
    QString m_tokenId;
    int m_quickCrashes = 0;
    int m_launches = 0;
    bool m_stopping = false;
    // The log relay's budget for the current second
    QElapsedTimer m_logWindow;
    int m_logLines = 0;
    qint64 m_logBytes = 0;
    int m_logDropped = 0;
    bool m_midLine = false;   // the rest of a line that was cut
    qint64 m_relayed = 0;
    qint64 m_droppedTotal = 0;
};

#endif // FRONTENDSUPERVISOR_H
