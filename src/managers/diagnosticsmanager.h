#ifndef DIAGNOSTICSMANAGER_H
#define DIAGNOSTICSMANAGER_H

// Field diagnostics: lets the user read and get the logs out of the head
// unit without a laptop (Settings > About > Diagnostics). Mirrors
// backend/diagnostics_manager.py. Log files themselves come from
// src/util/logger.cpp (C++) / backend/logging_config.py (Python).

#include <QObject>
#include <QString>

class DiagnosticsManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString logDir READ logDir CONSTANT)
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
    Q_PROPERTY(QString backendName READ backendName CONSTANT)
    Q_PROPERTY(QString deviceInfo READ deviceInfo CONSTANT)
    // Where exportLogs() last copied the files ("" until it has run)
    Q_PROPERTY(QString lastExportPath READ lastExportPath NOTIFY lastExportPathChanged)
    // Desktop only: the OS file manager can open the log folder
    Q_PROPERTY(bool canOpenFolder READ canOpenFolder CONSTANT)

public:
    explicit DiagnosticsManager(QObject *parent = nullptr);

    QString logDir() const;
    QString appVersion() const;
    QString backendName() const { return QStringLiteral("C++"); }
    QString deviceInfo() const;
    QString lastExportPath() const { return m_lastExportPath; }
    bool canOpenFolder() const;

    // Last maxLines lines of the main log followed by the error log
    Q_INVOKABLE QString recentLogLines(int maxLines = 200) const;
    // recentLogLines() plus a header (version, device) onto the clipboard
    Q_INVOKABLE bool copyLogsToClipboard(int maxLines = 400);
    // Copy every log file (current + rotated) into
    // <Downloads>/OCTAVE-logs-<timestamp>/ where any app can attach them.
    // Returns the folder, "" on failure.
    Q_INVOKABLE QString exportLogs();
    // Desktop: open the log folder in the file manager
    Q_INVOKABLE bool openLogFolder();

signals:
    void lastExportPathChanged();

private:
    QStringList logFiles() const;
    QString m_lastExportPath;
};

#endif  // DIAGNOSTICSMANAGER_H
