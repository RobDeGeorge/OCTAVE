#include "devtoolsmanager.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QUrl>

namespace OctaveDev {

DevToolsManager::DevToolsManager(const QDir &projectRoot, QObject *parent)
    : QObject(parent)
    , m_root(projectRoot)
    , m_outDir(projectRoot.filePath(QStringLiteral("dev/screenshots/output")))
{
    m_outDir.mkpath(QStringLiteral("."));
    refreshCount();
}

QJsonObject DevToolsManager::loadManifest() const
{
    QFile f(m_outDir.filePath(QStringLiteral("manifest.json")));
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isObject())
            return doc.object();
    }
    return QJsonObject{{"screenshots", QJsonArray()}, {"created", QDateTime::currentDateTime().toString(Qt::ISODate)}};
}

void DevToolsManager::saveManifest(QJsonObject manifest) const
{
    manifest["updated"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    QFile f(m_outDir.filePath(QStringLiteral("manifest.json")));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
}

void DevToolsManager::refreshCount()
{
    const int count = loadManifest().value("screenshots").toArray().size();
    if (count != m_count) {
        m_count = count;
        emit screenshotCountChanged(count);
    }
}

void DevToolsManager::setStatus(const QString &status)
{
    m_status = status;
    emit screenshotStatusChanged(status);
}

void DevToolsManager::appendOutput(const QString &text)
{
    m_output += text + QLatin1Char('\n');
    emit screenshotOutputChanged(m_output);
}

void DevToolsManager::captureWindow(const QString &name, const QString &description)
{
    // Called from QML after the overlays are hidden: grabWindow() captures
    // only the rendered scene
    if (!m_window) {
        appendOutput(QStringLiteral("[err] No window reference set"));
        return;
    }
    const QImage image = m_window->grabWindow();
    if (image.isNull()) {
        appendOutput(QStringLiteral("[err] grabWindow returned null image"));
        return;
    }
    static const QRegularExpression unsafe(QStringLiteral("[^\\w\\-]"));
    const QString safeName = QString(name).replace(unsafe, QStringLiteral("_"));
    QString path = m_outDir.filePath(safeName + QStringLiteral(".png"));
    if (QFile::exists(path))
        path = m_outDir.filePath(QStringLiteral("%1_%2.png").arg(safeName, QTime::currentTime().toString("HHmmss")));
    image.save(path, "PNG");

    QJsonObject manifest = loadManifest();
    QJsonArray shots = manifest.value("screenshots").toArray();
    shots.append(QJsonObject{
        {"file", m_root.relativeFilePath(path)},
        {"name", safeName},
        {"description", description},
        {"timestamp", QDateTime::currentDateTime().toString(Qt::ISODate)},
        {"size", QJsonObject{{"width", image.width()}, {"height", image.height()}}},
    });
    manifest["screenshots"] = shots;
    saveManifest(manifest);
    refreshCount();
    appendOutput(QStringLiteral("Saved: %1").arg(m_root.relativeFilePath(path)));
}

void DevToolsManager::beginCapture()
{
    m_output.clear();
    m_capturing = true;
    emit isCapturingChanged(true);
    setStatus(QStringLiteral("Capturing..."));
}

void DevToolsManager::endCapture()
{
    m_capturing = false;
    emit isCapturingChanged(false);
    refreshCount();
    setStatus(QStringLiteral("Done! %1 screenshots captured").arg(m_count));
}

void DevToolsManager::clearScreenshots()
{
    for (const QString &f : m_outDir.entryList({QStringLiteral("*.png")}, QDir::Files))
        m_outDir.remove(f);
    saveManifest(QJsonObject{{"screenshots", QJsonArray()}, {"created", QDateTime::currentDateTime().toString(Qt::ISODate)}});
    m_count = 0;
    emit screenshotCountChanged(0);
    setStatus(QStringLiteral("Screenshots cleared"));
    m_output.clear();
    appendOutput(QStringLiteral("All screenshots cleared."));
}

void DevToolsManager::generateGallery()
{
    const QJsonArray shots = loadManifest().value("screenshots").toArray();
    if (shots.isEmpty()) {
        appendOutput(QStringLiteral("No screenshots to generate gallery from."));
        setStatus(QStringLiteral("No screenshots"));
        return;
    }
    QStringList lines{QStringLiteral("# OCTAVE Screenshots\n")};
    for (const QJsonValue &v : shots) {
        const QJsonObject e = v.toObject();
        const QString desc = e.value("description").toString(e.value("name").toString());
        lines << QStringLiteral("### %1\n").arg(desc) << QStringLiteral("![%1](%2)\n").arg(desc, e.value("file").toString());
    }
    const QString galleryPath = m_outDir.filePath(QStringLiteral("GALLERY.md"));
    QFile f(galleryPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        f.write(lines.join(QLatin1Char('\n')).toUtf8());
    appendOutput(QStringLiteral("Gallery: %1").arg(m_root.relativeFilePath(galleryPath)));
    setStatus(QStringLiteral("Gallery generated"));
}

void DevToolsManager::openOutputFolder()
{
    m_outDir.mkpath(QStringLiteral("."));
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_outDir.absolutePath()));
}

} // namespace OctaveDev
