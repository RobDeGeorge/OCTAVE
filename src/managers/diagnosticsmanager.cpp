#include "diagnosticsmanager.h"
#include "settingsmanager.h"

#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUrl>

Q_LOGGING_CATEGORY(lcDiag, "octave.diagnostics")

#ifndef OCTAVE_VERSION
#define OCTAVE_VERSION "dev"
#endif

DiagnosticsManager::DiagnosticsManager(QObject *parent) : QObject(parent) {}

QString DiagnosticsManager::logDir() const
{
    return SettingsManager::getAppDataDir() + QStringLiteral("/logs");
}

QString DiagnosticsManager::appVersion() const
{
    return QStringLiteral(OCTAVE_VERSION);
}

QString DiagnosticsManager::deviceInfo() const
{
    return QStringLiteral("%1 %2 (%3, %4)")
        .arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture(),
             QSysInfo::machineHostName(), QSysInfo::kernelVersion());
}

bool DiagnosticsManager::canOpenFolder() const
{
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    return false;
#else
    return true;
#endif
}

QStringList DiagnosticsManager::logFiles() const
{
    QDir dir(logDir());
    QStringList files = dir.entryList({QStringLiteral("octave*.log*")}, QDir::Files, QDir::Name);
    for (QString &f : files)
        f = dir.absoluteFilePath(f);
    return files;
}

static QString tailOf(const QString &path, int maxLines)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    // Read the last 256 KB at most; enough for a few hundred lines
    const qint64 window = 256 * 1024;
    if (f.size() > window)
        f.seek(f.size() - window);
    const QString text = QString::fromUtf8(f.readAll());
    QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.size() > maxLines)
        lines = lines.mid(lines.size() - maxLines);
    return lines.join(QLatin1Char('\n'));
}

QString DiagnosticsManager::recentLogLines(int maxLines) const
{
    const QString dir = logDir();
    const QString main = tailOf(dir + QStringLiteral("/octave-cpp.log"), maxLines);
    const QString errors = tailOf(dir + QStringLiteral("/octave-cpp-error.log"), maxLines / 4);
    QString out = main.isEmpty() ? QStringLiteral("(no log yet)") : main;
    if (!errors.isEmpty())
        out += QStringLiteral("\n\n---- errors ----\n") + errors;
    return out;
}

bool DiagnosticsManager::copyLogsToClipboard(int maxLines)
{
    QClipboard *cb = QGuiApplication::clipboard();
    if (!cb)
        return false;
    cb->setText(QStringLiteral("OCTAVE %1 (%2 backend) on %3\n%4\n\n%5")
                    .arg(appVersion(), backendName(), deviceInfo(),
                         QDateTime::currentDateTime().toString(Qt::ISODate), recentLogLines(maxLines)));
    qCInfo(lcDiag) << "Logs copied to clipboard";
    return true;
}

QString DiagnosticsManager::exportLogs()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString target = base + QStringLiteral("/OCTAVE-logs-")
                           + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    if (!QDir().mkpath(target)) {
        qCWarning(lcDiag) << "Could not create" << target;
        return {};
    }
    int copied = 0;
    for (const QString &src : logFiles()) {
        if (QFile::copy(src, target + QLatin1Char('/') + QFileInfo(src).fileName()))
            ++copied;
    }
    QFile info(target + QStringLiteral("/device-info.txt"));
    if (info.open(QIODevice::WriteOnly | QIODevice::Text)) {
        info.write(QStringLiteral("OCTAVE %1 (%2 backend)\n%3\nexported %4\n")
                       .arg(appVersion(), backendName(), deviceInfo(), QDateTime::currentDateTime().toString(Qt::ISODate))
                       .toUtf8());
    }
    qCInfo(lcDiag) << "Exported" << copied << "log files to" << target;
    m_lastExportPath = target;
    emit lastExportPathChanged();
    return target;
}

bool DiagnosticsManager::openLogFolder()
{
    if (!canOpenFolder())
        return false;
    return QDesktopServices::openUrl(QUrl::fromLocalFile(logDir()));
}
