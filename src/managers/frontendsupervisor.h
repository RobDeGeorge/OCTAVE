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
//     (exitedCleanly);
//   - anything else (a crash, a non-zero code, failing to start): started
//     again after a back-off (1 s, doubling to 30 s). A run longer than a
//     minute forgives the crashes before it;
//   - kMaxQuickCrashes crashes in a row, each within a minute of starting:
//     gaveUp(reason), and main.cpp writes USE_QML and starts OCTAVE again
//     on the QML UI.
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
    };

    FrontendSupervisor(const FrontendManifest &manifest, const QString &dir, ServicePairing *pairing, quint16 port,
                       QObject *parent = nullptr);
    ~FrontendSupervisor() override;

    void setOptions(const Options &options) { m_options = options; }
    void start();
    void stop();

    bool running() const;
    int launches() const { return m_launches; }
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
};

#endif // FRONTENDSUPERVISOR_H
