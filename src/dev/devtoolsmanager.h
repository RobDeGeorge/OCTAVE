#ifndef OCTAVE_DEV_DEVTOOLSMANAGER_H
#define OCTAVE_DEV_DEVTOOLSMANAGER_H

// Backing object for the dev tools panel (src/dev/qml/DevToolsPanel.qml),
// exposed to QML as `devToolsManager` in dev mode only: window grabs into dev/screenshots/output with a
// manifest.json, a markdown gallery, and open/clear of the output folder.

#include <QDir>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QQuickWindow>

namespace OctaveDev {

class DevToolsManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString screenshotStatus READ screenshotStatus NOTIFY screenshotStatusChanged)
    Q_PROPERTY(QString screenshotOutput READ screenshotOutput NOTIFY screenshotOutputChanged)
    Q_PROPERTY(int screenshotCount READ screenshotCount NOTIFY screenshotCountChanged)
    Q_PROPERTY(bool isCapturing READ isCapturing NOTIFY isCapturingChanged)

public:
    explicit DevToolsManager(const QDir &projectRoot, QObject *parent = nullptr);

    void setWindow(QQuickWindow *window) { m_window = window; }

    QString screenshotStatus() const { return m_status; }
    QString screenshotOutput() const { return m_output; }
    int screenshotCount() const { return m_count; }
    bool isCapturing() const { return m_capturing; }

    Q_INVOKABLE void captureWindow(const QString &name, const QString &description);
    Q_INVOKABLE void beginCapture();
    Q_INVOKABLE void endCapture();
    Q_INVOKABLE void clearScreenshots();
    Q_INVOKABLE void generateGallery();
    Q_INVOKABLE void openOutputFolder();

signals:
    void screenshotStatusChanged(const QString &status);
    void screenshotOutputChanged(const QString &output);
    void screenshotCountChanged(int count);
    void isCapturingChanged(bool capturing);
    // Tell QML to hide / show dev overlays for clean screenshots
    void hideOverlaysRequested();
    void showOverlaysRequested();

private:
    QJsonObject loadManifest() const;
    void saveManifest(QJsonObject manifest) const;
    void refreshCount();
    void setStatus(const QString &status);
    void appendOutput(const QString &text);

    QDir m_root;
    QDir m_outDir;
    QPointer<QQuickWindow> m_window;
    QString m_status = QStringLiteral("idle");
    QString m_output;
    int m_count = 0;
    bool m_capturing = false;
};

} // namespace OctaveDev

#endif // OCTAVE_DEV_DEVTOOLSMANAGER_H
