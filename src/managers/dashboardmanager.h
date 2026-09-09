// dashboardmanager.h
//
// Enumerates built-in dashboard presets and user-authored dashboards,
// exposes the merged list to QML, and handles save/delete/duplicate.
// Phase 2 of the dashboards roadmap (see TODO/dashboards-roadmap.md).
//
// Lifecycle: constructed by main.cpp, which then calls setPresetsDir() and
// setUserDir() to tell the manager where to look. Scanning happens on
// setUserDir() (so the app can do both in sequence without two scans) and
// any time refresh() is invoked.
//
// Built-in vs user precedence: ids must be unique across both sources. If
// a user dashboard shares an id with a built-in, the user JSON is rejected
// at load time (logged + skipped). Duplicating a built-in auto-generates a
// unique user id derived from the new label.

#ifndef DASHBOARDMANAGER_H
#define DASHBOARDMANAGER_H

#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QSet>
#include <QFileSystemWatcher>
#include <QTimer>

class DashboardManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList dashboards READ dashboards NOTIFY dashboardsChanged)

public:
    explicit DashboardManager(QObject *parent = nullptr);

    // Configure source directories. Both should be absolute paths. Call
    // setPresetsDir() first, then setUserDir() — the second call triggers
    // an initial scan.
    void setPresetsDir(const QString &absolutePath);
    void setUserDir(const QString &absolutePath);

    // Merged list: each entry is { id, label, builtIn, path }. Sorted so
    // built-ins come first (in file-name order), then user dashboards.
    QVariantList dashboards() const { return m_dashboards; }

    // Returns the fully-parsed spec object for `id`, or an empty map if
    // the id doesn't exist or the file can't be parsed. QML can inspect
    // the result with `if (Object.keys(spec).length === 0) …`.
    Q_INVOKABLE QVariant loadDashboard(const QString &id);

    // True if `id` refers to a built-in preset (read-only).
    Q_INVOKABLE bool isBuiltIn(const QString &id) const;

public slots:
    // Writes a user dashboard JSON file. `spec` must include at least `id`
    // and `label`. Returns the saved id on success, empty string on
    // failure (id collision with built-in, I/O error, etc.).
    QString saveDashboard(const QVariantMap &spec);

    // Deletes a user dashboard file. Built-ins cannot be deleted (returns
    // false). Returns true on success.
    bool deleteDashboard(const QString &id);

    // Clones a source dashboard (built-in or user) into a new user dashboard
    // with the given label. The new id is auto-derived from the label +
    // a numeric suffix if needed to ensure uniqueness. Returns the new id
    // on success, empty string on failure.
    QString duplicateDashboard(const QString &sourceId, const QString &newLabel);

    // Re-scan both directories. Emits dashboardsChanged() afterwards.
    // Call this after editing JSON externally while the app is running.
    void refresh();

signals:
    void dashboardsChanged();

private:
    QString m_presetsDir;
    QString m_userDir;
    QVariantList m_dashboards;

    // User-dir hot reload: a JSON dropped into / removed from the user
    // dashboards folder while the app runs shows up in the chooser without
    // a restart. Directory events are debounced and only trigger a rescan
    // when the (name, mtime, size) signature actually changed — our own
    // atomic saves already rescanned, so they don't double-fire.
    QFileSystemWatcher m_watcher;
    QTimer m_rescanDebounce;
    QString m_userDirSignature;
    void watchUserDir();
    QString userDirSignatureNow() const;
    void onUserDirChanged();

    void rescanAll();
    void ensureUserDir() const;

    // Read minimal header info from a JSON file (just id + label). Empty
    // map on parse/io failure or missing fields.
    QVariantMap readHeader(const QString &absolutePath) const;

    // Read the full spec from a JSON file. Empty map on failure.
    QVariantMap readFullSpec(const QString &absolutePath) const;

    // Write a spec to `absolutePath` atomically (tmp + rename).
    bool writeSpec(const QString &absolutePath, const QVariantMap &spec);

    // Lookup helpers.
    QVariantMap findEntry(const QString &id) const;
    QSet<QString> existingIds() const;

    // Generate a file-safe, collision-free id from a human label.
    QString uniqueUserIdFromLabel(const QString &label) const;
    static QString slugify(const QString &label);
};

#endif // DASHBOARDMANAGER_H
