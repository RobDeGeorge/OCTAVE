#ifndef OCTAVE_DEV_UIDRIVER_H
#define OCTAVE_DEV_UIDRIVER_H

// GUI-thread half of the command server: navigation through Main.qml's functions,
// window grabs, MP4 recording through ffmpeg, and finding / poking / clicking
// QML items by objectName or text. Every method runs on the GUI thread
// (handlers reach it through CommandServer::onGui()).

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QSize>
#include <QTimer>
#include <QVariantMap>

class QProcess;
class QQmlApplicationEngine;
class QQuickItem;
class QQuickWindow;

namespace OctaveDev {

class UiDriver : public QObject
{
    Q_OBJECT

public:
    explicit UiDriver(QQmlApplicationEngine *engine, QObject *parent = nullptr);
    ~UiDriver() override;

    QQuickWindow *window() const;

    // Navigation (Main.qml functions)
    void navigateTo(const QString &qmlFile);
    void navigateToSettings(const QString &section);
    void navigateHome();
    void navigateBack();
    QString currentPage() const;

    void setWindowSize(int w, int h);
    void setDevChrome(bool visible);
    bool captureTo(const QString &path);

    // Recording (README / demo clips)
    void recordStart(const QString &path, int fps);
    void recordStop();
    QVariantMap recordStatus() const;

    // Introspection + synthetic input
    QVariant findItem(const QString &name) const;
    QVariant findText(const QString &text, const QString &prop) const;
    QVariantList listItems(const QString &prefix) const;
    QVariant getProperty(const QString &name, const QString &prop) const;
    bool setProperty(const QString &name, const QString &prop, const QVariant &value);
    bool callMethod(const QString &name, const QString &method);
    void click(double x, double y);
    void longPress(double x, double y, int ms);
    void drag(double x1, double y1, double x2, double y2, int steps);
    QVariant typeText(const QString &text, bool enter, const QString &field);

    // Geometry + effective visibility of an item
    static QVariantMap itemGeometry(QQuickItem *item);

private:
    QObject *findObject(const QString &name) const;   // "root" = the window itself
    void mouse(int kind, double x, double y);
    void recordTick();

    QQmlApplicationEngine *m_engine;

    QProcess *m_recProc = nullptr;
    QTimer m_recTimer;
    QElapsedTimer m_recClock;
    QString m_recPath;
    QString m_recError;
    QSize m_recSize{0, 0};
    int m_recFps = 0;
    int m_recFrames = 0;
    int m_recDropped = 0;
    int m_recRepeated = 0;
};

} // namespace OctaveDev

#endif // OCTAVE_DEV_UIDRIVER_H
