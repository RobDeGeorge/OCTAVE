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
//
// Deleting a built-in hides it instead (the preset files ship read-only):
// hidden ids persist in <userDir>/.hidden-builtins (a JSON array of ids) and
// restoreBuiltInDashboards() brings them all back. A hidden preset still
// reserves its id, so a user dashboard can't take it over.

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
    // Service permissions (src/service/servicescopes.h)
    Q_CLASSINFO("octave.scope", "files")
    Q_CLASSINFO("octave.scope.read", "arrange canPlace fitAt isBuiltIn loadDashboard minSpan nearestFree normalizedSpec refresh rescale sizeOptions uniqueIdForLabel")
    Q_CLASSINFO("octave.experimental", "arrange minSpan nearestFree rescale sizeOptions")
    Q_PROPERTY(QVariantList dashboards READ dashboards NOTIFY dashboardsChanged)
    // Human-readable reason the last export/import failed ("" after a
    // success) — the chooser shows it in its status line.
    Q_PROPERTY(QString lastShareError READ lastShareError NOTIFY lastShareErrorChanged)
    // Why the last loadDashboard / saveDashboard failed ("" after one that
    // worked): their return values stay as they were
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    // Where exportDashboard last wrote ("" until it has); a service client
    // downloads it from GET /export/dashboard
    Q_PROPERTY(QString lastExportPath READ lastExportPath NOTIFY lastExportPathChanged)
    // Built-in presets the user deleted (hidden) — the chooser shows
    // "Restore defaults" while this is non-zero.
    Q_PROPERTY(int hiddenBuiltInCount READ hiddenBuiltInCount NOTIFY dashboardsChanged)
    // kSupportedSchema, for DashboardRenderer's "needs a newer OCTAVE" placeholder
    Q_PROPERTY(int supportedSchema READ supportedSchema CONSTANT)
    // The widget types a dashboard can use (frontend/dashboards/
    // widget-catalog.json, published as docs/api/widget-catalog.json):
    // {schema, widgets: [{type, displayName, glyph, defaultColSpan,
    // defaultRowSpan, supportedKinds, labelProps, rangeProps, colorProps,
    // editableProps}], colorSwatches}. Empty if the file is missing.
    Q_PROPERTY(QVariantMap widgetCatalog READ widgetCatalog CONSTANT)

public:
    explicit DashboardManager(QObject *parent = nullptr);

    // Configure source directories. Both should be absolute paths. Call
    // setPresetsDir() first, then setUserDir() — the second call triggers
    // an initial scan.
    void setPresetsDir(const QString &absolutePath);
    void setUserDir(const QString &absolutePath);
    // Before setUserDir() (the scan normalises with it); main.cpp passes
    // frontend/dashboards/widget-catalog.json
    void setWidgetCatalogFile(const QString &absolutePath);
    QVariantMap widgetCatalog() const { return m_widgetCatalog; }

    // Merged list: each entry is { id, label, builtIn, path }. Sorted so
    // built-ins come first (in file-name order), then user dashboards.
    QVariantList dashboards() const { return m_dashboards; }

    // Returns the fully-parsed spec object for `id`, or an empty map if
    // the id doesn't exist or the file can't be parsed. QML can inspect
    // the result with `if (Object.keys(spec).length === 0) …`.
    Q_INVOKABLE QVariant loadDashboard(const QString &id);

    // True if `id` refers to a built-in preset (read-only).
    Q_INVOKABLE bool isBuiltIn(const QString &id) const;

    // A file-safe id for a new dashboard named `label`: the slug of the
    // label, with -2, -3, ... appended until it is free. Never a built-in id
    // (hidden ones included) or kParameterCardsId. The editor calls this
    // before saving a new dashboard; duplicate and import use it too.
    Q_INVOKABLE QString uniqueIdForLabel(const QString &label) const;

    // A spec as save and load would leave it (see normalizeSpec), for specs
    // that don't come from a file: the editor's rescued draft. Empty if it
    // isn't a usable dashboard. An absent id stays absent.
    Q_INVOKABLE QVariantMap normalizedSpec(const QVariantMap &spec) const;

    // Grid placement for the editor (src/util/dashboardgrid.h): whether a
    // rectangle fits without covering another cell (`exceptIndex` = the cell
    // being moved, -1 for none), and the span a new widget shrinks to at
    // (col, row) as {col, row, colSpan, rowSpan}.
    Q_INVOKABLE bool canPlace(const QVariantList &cells, int gridColumns, int gridRows, int col, int row,
                              int colSpan, int rowSpan, int exceptIndex) const;
    Q_INVOKABLE QVariantMap fitAt(const QVariantList &cells, int gridColumns, int gridRows, int col, int row,
                                  int colSpan, int rowSpan) const;

    // Cell `index` moved / resized to (col, row, colSpan, rowSpan), the
    // cells it lands on swapped or pushed aside (DashboardGrid::arrange):
    // {ok, cells, moved: [indexes], swapped, reason}. Nothing is saved.
    Q_INVOKABLE QVariantMap arrange(const QVariantList &cells, int gridColumns, int gridRows, int index,
                                    int col, int row, int colSpan, int rowSpan) const;

    // The free spot of colSpan x rowSpan nearest (col, row), cell
    // `exceptIndex` ignored (-1 for none): {ok, col, row, colSpan, rowSpan}
    Q_INVOKABLE QVariantMap nearestFree(const QVariantList &cells, int gridColumns, int gridRows, int col, int row,
                                        int colSpan, int rowSpan, int exceptIndex) const;

    // The cells laid out again on a toColumns x toRows grid (the editor's
    // grid density, DashboardGrid::rescale): {ok, cells, reason}
    Q_INVOKABLE QVariantMap rescale(const QVariantList &cells, int fromColumns, int fromRows, int toColumns,
                                    int toRows) const;

    // Sizes for a widget type on a grid whose cells are cellWidth x
    // cellHeight dp with `spacing` dp gutters, from the catalog's minSize
    // and aspect: the smallest readable span {colSpan, rowSpan}, and the
    // S / M / L options [{label, colSpan, rowSpan}]
    Q_INVOKABLE QVariantMap minSpan(const QString &type, int gridColumns, int gridRows, double cellWidth,
                                    double cellHeight, double spacing) const;
    Q_INVOKABLE QVariantList sizeOptions(const QString &type, int gridColumns, int gridRows, double cellWidth,
                                         double cellHeight, double spacing) const;

    QString lastShareError() const { return m_lastShareError; }
    QString lastError() const { return m_lastError; }
    QString lastExportPath() const { return m_lastExportPath; }
    int hiddenBuiltInCount() const { return m_hiddenBuiltIns.size(); }
    int supportedSchema() const { return kSupportedSchema; }

    // OBDMenu's built-in Parameter Cards entry. Not a file, but it shares the
    // id space: no dashboard may take it.
    static constexpr const char *kParameterCardsId = "grid";

    // Highest dashboard `schema` this build understands. Imports declaring a
    // newer schema are refused rather than half-rendered.
    static constexpr int kSupportedSchema = 1;

public slots:
    // Writes a user dashboard JSON file, normalised first (normalizeSpec).
    // `spec` must include a `label`; with no `id` it gets
    // uniqueIdForLabel(label), a new dashboard (an existing id overwrites
    // that user dashboard). Returns the saved id on success, empty string
    // on failure (no label, id collision with a built-in or
    // kParameterCardsId, newer schema, I/O error, etc.).
    QString saveDashboard(const QVariantMap &spec);

    // Deletes a user dashboard file, or hides a built-in preset (persisted
    // in .hidden-builtins). Returns true on success.
    bool deleteDashboard(const QString &id);

    // Un-hides every built-in preset deleted with deleteDashboard().
    // Returns true if anything was restored.
    bool restoreBuiltInDashboards();

    // Clones a source dashboard (built-in or user) into a new user dashboard
    // with the given label. The new id is auto-derived from the label +
    // a numeric suffix if needed to ensure uniqueness. Returns the new id
    // on success, empty string on failure.
    QString duplicateDashboard(const QString &sourceId, const QString &newLabel);

    // ── Sharing ──────────────────────────────────────────────────────
    // Export writes the spec to <Downloads>/OCTAVE-dashboards/<id>.json and
    // returns that path ("" on failure). The clipboard variant copies the
    // same JSON as text, for pasting into a chat or forum post.
    QString exportDashboard(const QString &id);
    bool copyDashboardToClipboard(const QString &id);

    // Import a shared dashboard as a NEW user dashboard (never overwrites:
    // the id is always regenerated from the label). Accepts a local path,
    // a file:// URL or an Android content:// URL. The spec is validated and
    // reduced to the known schema-1 keys first (see normalizeSpec).
    // Returns the new id, or "" with lastShareError set.
    QString importDashboard(const QString &fileOrUrl);
    QString importDashboardFromClipboard();
    QString importDashboardFromText(const QString &json);

    // Re-scan both directories. Emits dashboardsChanged() afterwards.
    // Call this after editing JSON externally while the app is running.
    void refresh();

signals:
    void dashboardsChanged();
    void lastShareErrorChanged();
    void lastErrorChanged();
    void lastExportPathChanged();

private:
    QString m_lastShareError;
    void setShareError(const QString &message);
    QString m_lastError;
    QString m_lastExportPath;
    void setError(const QString &message);

    // The one set of spec rules, applied on import, save and load: known
    // schema-1 keys only (JSON numbers, strings and bools are not numbers),
    // the caps above, a cell off the grid moved back in (shrunk only if wider
    // than the grid), props scalar, and no two cells overlapping (in file
    // order the earlier wins; a later one shrinks toward 1x1, else is
    // dropped). With a widget catalog, a known type's editableProps are
    // held to their kind: a bool must be a bool, an int / real a number
    // (clamped to min / max, an int rounded), an enum one of its options,
    // a string a string; anything else is dropped (the widget's default).
    // Other props (title, unit, min, max, colours) are left as they are.
    // Unknown widget types and paramIds are kept: the QML
    // WidgetCatalog knows the types, the renderer skips or leaves them
    // unbound, and a newer build may know them. Import drops the id (the
    // caller makes a new one) and refuses a bad schema or no cell list; save
    // and load keep the id and need it, a save only a file-safe one. A newer
    // schema is refused (loadDashboard passes such a file through untouched).
    // Returns an empty map (and sets *error) when the data isn't usable.
    enum class SpecUse { Import, Save, Load };
    static QVariantMap normalizeSpec(const QVariantMap &raw, SpecUse use, QString *error,
                                     const QVariantMap &catalog = {});
    QString importSpec(const QVariantMap &raw);

    QString m_presetsDir;
    QVariantMap m_widgetCatalog;
    QString m_userDir;
    QVariantList m_dashboards;

    // Ids of every preset on disk (hidden or not) and the hidden subset.
    QSet<QString> m_builtInIds;
    QSet<QString> m_hiddenBuiltIns;
    QString hiddenBuiltInsPath() const;
    void loadHiddenBuiltIns();
    bool saveHiddenBuiltIns() const;

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
    static QString slugify(const QString &label);
};

#endif // DASHBOARDMANAGER_H
