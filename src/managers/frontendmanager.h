#ifndef FRONTENDMANAGER_H
#define FRONTENDMANAGER_H

// Installed frontends (TODO/frontend-platform.md step 4): other screens
// for OCTAVE, each a folder with an octave-frontend.json
// (frontendmanifest.h), kept in <config>/frontends/<id>/. The built-in QML
// UI is the default and the fallback; one installed frontend can be the
// active one, chosen here and used from the next start:
//   - a web frontend is served at GET / (instead of the reference client;
//     --web-root still wins) and turns the service on;
//   - an exec frontend is the screen: OCTAVE runs headless and starts and
//     supervises it (frontendsupervisor.h) with a token holding only the
//     scopes its manifest asks for.
//
// Installing copies a folder, or unpacks a .zip, into a staging folder,
// checks it (the manifest, a protocol this OCTAVE speaks, a command for
// this platform, no symlinks, no path leaving the folder, at most
// kMaxFiles files and kMaxBytes in all) and only then moves it into place;
// nothing in it is run. Installing an id that is already there replaces it.
//
// Files under <config>/frontends/: ACTIVE (the active id; absent = QML)
// and USE_QML (the escape hatch: while it exists an exec frontend is not
// started and the QML UI shows; its text says why, the supervisor writes
// it after repeated crashes and anyone can create it by hand on the boot
// volume). `octave --qml` does the same for one run.
//
// Trying a screen (tryFrontend): an exec frontend can be tried for a few
// minutes. TRIAL holds {id, previous, seconds}; OCTAVE restarts on it
// (restartRequested), renames TRIAL to TRIAL.running at the start
// (beginTrial) and counts down (trialSecondsLeft); at zero, or when the
// frontend gives up, the previous screen is back (endTrial) and OCTAVE
// restarts on it. keepTrial() (the user liked it; any connected frontend
// may call it, since the user chose to try this one) makes it the screen
// for good. A TRIAL.running found at a start means the trial run never
// ended on its own (power cut, crash): the previous screen comes back
// then too, so turning the car off and on always ends a trial.
//
// The escape: VolumeController::escapeGesture (mute pressed five times
// within four seconds, from any input: the knob, the gesture sensor, a
// frontend) ends an exec frontend's run, writes USE_QML and restarts on
// the QML UI (main.cpp).
//
// Service object `frontendManager`: changes need the `system` permission.

#include "frontendmanifest.h"

#include <QObject>
#include <QTimer>
#include <QVariantList>

class FrontendManager : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("octave.scope", "system")
    Q_CLASSINFO("octave.scope.read", "manifestOf keepTrial")
    // [{id, name, version, description, kind, protocol, scopes, command,
    //   active, compatible, problem, path, preview}]; preview is the URL of
    //   the manifest's picture, "" without one
    Q_PROPERTY(QVariantList frontends READ frontends NOTIFY frontendsChanged)
    // The one used from the next start, "" for the built-in QML UI
    Q_PROPERTY(QString activeId READ activeId NOTIFY activeChanged)
    // What this run shows ("" = the QML UI), decided at start
    Q_PROPERTY(QString runningId READ runningId CONSTANT)
    // USE_QML is present, and its text
    Q_PROPERTY(bool forcedQml READ forcedQml NOTIFY activeChanged)
    Q_PROPERTY(QString forcedQmlReason READ forcedQmlReason NOTIFY activeChanged)
    // Why the last install / remove / setActive failed ("" after a success)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    // Seconds until a tried screen goes back to the previous one; 0 when
    // this run is not a trial (or it was kept)
    Q_PROPERTY(int trialSecondsLeft READ trialSecondsLeft NOTIFY trialChanged)
    Q_CLASSINFO("octave.experimental.trialSecondsLeft", "trying a screen is new in 0.9.6")
    Q_CLASSINFO("octave.experimental.tryFrontend", "trying a screen is new in 0.9.6")
    Q_CLASSINFO("octave.experimental.keepTrial", "trying a screen is new in 0.9.6")
    Q_CLASSINFO("octave.experimental.restartNow", "new in 0.9.6")
    Q_CLASSINFO("octave.experimental.trialChanged", "trying a screen is new in 0.9.6")
    Q_CLASSINFO("octave.experimental.restartRequested", "new in 0.9.6")

public:
    static constexpr int kMaxFiles = 10000;
    static constexpr qint64 kMaxBytes = 256LL * 1024 * 1024;
    static constexpr int kMaxDepth = 24;
    static constexpr qint64 kMaxPreviewBytes = 4LL * 1024 * 1024;

    explicit FrontendManager(QObject *parent = nullptr);

    // <config>/frontends (AppCore); made if missing
    void setRoot(const QString &dir);
    QString root() const { return m_root; }
    void setRunningId(const QString &id) { m_runningId = id; }

    QVariantList frontends() const;
    QString activeId() const;
    QString runningId() const { return m_runningId; }
    bool forcedQml() const;
    QString forcedQmlReason() const;
    QString lastError() const { return m_lastError; }

    // The id installed, or "" (lastError says why). A path, or the file://
    // URL a QML picker gives
    Q_INVOKABLE QString installFromFolder(const QString &path);
    Q_INVOKABLE QString installFromZip(const QString &path);
    Q_INVOKABLE bool remove(const QString &id);
    // "" = the built-in QML UI. From the next start; choosing a frontend
    // also deletes USE_QML
    Q_INVOKABLE bool setActive(const QString &id);
    // Delete USE_QML, so the active exec frontend starts again next time
    Q_INVOKABLE void clearForcedQml();
    // The installed manifest as frontends() lists it, {} if none
    Q_INVOKABLE QVariantMap manifestOf(const QString &id) const;
    // Make the exec frontend `id` the screen for `seconds` (clamped to
    // kMinTrialSeconds..kMaxTrialSeconds), then go back to the current
    // one by itself. OCTAVE restarts at once (restartRequested)
    Q_INVOKABLE bool tryFrontend(const QString &id, int seconds);
    // Keep the screen being tried: no going back when the time is up
    Q_INVOKABLE void keepTrial();
    // Restart OCTAVE now, so a choice made with setActive takes effect
    Q_INVOKABLE void restartNow();

    int trialSecondsLeft() const { return m_trialLeft; }
    // main.cpp, in a trial run: count down from `seconds`; trialExpired at 0
    void startTrialCountdown(int seconds);

    // ── At start, before AppCore (main.cpp) ──
    struct Boot {
        enum Kind { Qml, Web, Exec };
        Kind kind = Qml;
        FrontendManifest manifest;
        QString dir;       // the frontend's folder
        QString note;      // why the QML UI, when something else was active
    };
    // What to show: the active frontend unless `qmlFlag`, USE_QML (exec
    // only), or it is missing, broken or can't run here
    static Boot bootChoice(const QString &root, bool qmlFlag);
    // Write USE_QML with `reason`
    static void forceQml(const QString &root, const QString &reason);
    struct Trial {
        QString id;         // the frontend being tried; empty = no trial
        QString previous;   // the screen to go back to ("" = the QML UI)
        int seconds = 0;
        QString note;       // what beginTrial did, for the log
        bool active() const { return !id.isEmpty(); }
    };
    static constexpr int kMinTrialSeconds = 15;
    static constexpr int kMaxTrialSeconds = 900;
    static constexpr int kDefaultTrialSeconds = 120;
    // At start, before bootChoice: TRIAL becomes TRIAL.running and is
    // returned; a TRIAL.running left by a run that never ended its trial
    // is undone at once (the previous screen is back) and no trial runs
    static Trial beginTrial(const QString &root);
    // Undo the running trial: the previous screen is ACTIVE again
    static void endTrial(const QString &root);
    // The installed manifest of `id` under `root`, checked; empty id if bad
    static FrontendManifest load(const QString &root, const QString &id, QStringList *errors);
    // Why `m` can't run in this build ("" if it can)
    static QString problemWith(const FrontendManifest &m);

signals:
    void frontendsChanged();
    void activeChanged();
    void lastErrorChanged();
    // An install finished (id) for the Settings page
    void installed(const QString &id);
    void trialChanged();
    // The time for the screen being tried is up (main.cpp goes back)
    void trialExpired();
    // tryFrontend / restartNow: main.cpp quits and starts OCTAVE again
    void restartRequested();

private:
    QString fail(const QString &why);
    bool refuse(const QString &why);
    QString finishInstall(const QString &staging);
    QString newStaging() const;

    QString m_root;
    QString m_runningId;
    QString m_lastError;
    QTimer *m_trialTimer = nullptr;
    int m_trialLeft = 0;
};

#endif // FRONTENDMANAGER_H
