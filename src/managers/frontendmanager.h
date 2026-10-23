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
// Service object `frontendManager`: changes need the `system` permission.

#include "frontendmanifest.h"

#include <QObject>
#include <QVariantList>

class FrontendManager : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("octave.scope", "system")
    Q_CLASSINFO("octave.scope.read", "manifestOf")
    // [{id, name, version, description, kind, protocol, scopes, command,
    //   active, compatible, problem, path}]
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

public:
    static constexpr int kMaxFiles = 10000;
    static constexpr qint64 kMaxBytes = 256LL * 1024 * 1024;
    static constexpr int kMaxDepth = 24;

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

private:
    QString fail(const QString &why);
    bool refuse(const QString &why);
    QString finishInstall(const QString &staging);
    QString newStaging() const;

    QString m_root;
    QString m_runningId;
    QString m_lastError;
};

#endif // FRONTENDMANAGER_H
