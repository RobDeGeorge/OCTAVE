#include "uidriver.h"

#include "commandserver.h"

#include <QCoreApplication>
#include <QImage>
#include <QKeyEvent>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QWheelEvent>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <functional>

namespace OctaveDev {

namespace {
// Depth-first over the visual tree, popups included (the Overlay lives under
// the window's contentItem). `visit` returns true to stop.
bool walk(QQuickItem *item, const std::function<bool(QQuickItem *)> &visit)
{
    if (!item)
        return false;
    if (visit(item))
        return true;
    const auto children = item->childItems();
    for (QQuickItem *child : children) {
        if (walk(child, visit))
            return true;
    }
    return false;
}
} // namespace

UiDriver::UiDriver(QQmlApplicationEngine *engine, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
{
    m_recTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_recTimer, &QTimer::timeout, this, &UiDriver::recordTick);
}

UiDriver::~UiDriver()
{
    recordStop();
}

QQuickWindow *UiDriver::window() const
{
    const auto roots = m_engine->rootObjects();
    return roots.isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(roots.first());
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

void UiDriver::navigateTo(const QString &qmlFile)
{
    if (QQuickWindow *w = window())
        invokeByName(w, "navigateTo", {qmlFile});
}

void UiDriver::navigateToSettings(const QString &section)
{
    if (QQuickWindow *w = window())
        invokeByName(w, "navigateToSettings", {section});
}

void UiDriver::navigateHome()
{
    if (QQuickWindow *w = window())
        invokeByName(w, "navigateHome");
}

void UiDriver::navigateBack()
{
    if (QQuickWindow *w = window())
        invokeByName(w, "navigateBack");
}

QString UiDriver::currentPage() const
{
    QQuickWindow *w = window();
    QObject *stack = w ? w->findChild<QObject *>(QStringLiteral("stackView")) : nullptr;
    if (!stack)
        return QStringLiteral("unknown");
    QObject *current = stack->property("currentItem").value<QObject *>();
    if (!current)
        return QStringLiteral("unknown");
    return current->objectName().isEmpty() ? QString::fromLatin1(current->metaObject()->className())
                                           : current->objectName();
}

void UiDriver::setWindowSize(int w, int h)
{
    // Main.qml's width/height handlers reflow Spacing
    if (QQuickWindow *win = window()) {
        win->setProperty("width", w);
        win->setProperty("height", h);
    }
}

void UiDriver::setDevChrome(bool visible)
{
    QQuickWindow *w = window();
    if (QObject *overlay = w ? w->findChild<QObject *>(QStringLiteral("devOverlay")) : nullptr)
        overlay->setProperty("overlayVisible", visible);
}

bool UiDriver::captureTo(const QString &path)
{
    QQuickWindow *w = window();
    if (!w)
        return false;
    const QImage image = w->grabWindow();
    return !image.isNull() && image.save(path, "PNG");
}

// ---------------------------------------------------------------------------
// Recording: grabWindow() on a GUI-thread timer, raw RGBA into ffmpeg's stdin
// ---------------------------------------------------------------------------

void UiDriver::recordStart(const QString &path, int fps)
{
    m_recError.clear();
    if (m_recProc) {
        m_recError = QStringLiteral("already recording");
        return;
    }
    QQuickWindow *w = window();
    if (!w) {
        m_recError = QStringLiteral("no window");
        return;
    }
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) {
        m_recError = QStringLiteral("ffmpeg not found");
        return;
    }
    // yuv420p needs even dimensions
    const int width = w->width() - w->width() % 2;
    const int height = w->height() - w->height() % 2;
    fps = std::clamp(fps > 0 ? fps : 30, 1, 60);
    m_recProc = new QProcess(this);
    m_recProc->setProgram(ffmpeg);
    m_recProc->setArguments({
        QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y"),
        QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("-pix_fmt"), QStringLiteral("rgba"),
        QStringLiteral("-s"), QStringLiteral("%1x%2").arg(width).arg(height), QStringLiteral("-r"), QString::number(fps),
        QStringLiteral("-i"), QStringLiteral("-"), QStringLiteral("-an"), QStringLiteral("-c:v"), QStringLiteral("libx264"),
        QStringLiteral("-preset"), QStringLiteral("veryfast"), QStringLiteral("-crf"), QStringLiteral("18"),
        QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), QStringLiteral("-movflags"), QStringLiteral("+faststart"),
        path,
    });
    m_recProc->setStandardOutputFile(QProcess::nullDevice());
    m_recProc->start();
    if (!m_recProc->waitForStarted(5000)) {
        m_recError = QStringLiteral("ffmpeg failed to start: %1").arg(m_recProc->errorString());
        delete m_recProc;
        m_recProc = nullptr;
        return;
    }
    m_recPath = path;
    m_recFps = fps;
    m_recSize = QSize(width, height);
    m_recFrames = m_recDropped = m_recRepeated = 0;
    m_recClock.start();
    m_recTimer.setInterval(1000 / fps);
    m_recTimer.start();
}

void UiDriver::recordTick()
{
    QQuickWindow *w = window();
    if (!m_recProc || !w)
        return;
    QImage img = w->grabWindow();
    if (img.isNull()) {
        ++m_recDropped;
        return;
    }
    if (img.size() != m_recSize) {
        // grabWindow() is in device pixels: with a device pixel ratio above 1
        // cropping would keep only the top-left of the UI, so scale
        img = img.scaled(m_recSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    img = img.convertToFormat(QImage::Format_RGBA8888);
    // Pace by wall clock: when rendering can't keep up the timer skips ticks,
    // and one frame per tick would play back fast. Repeat this frame up to
    // where real time says the clip should be.
    const int due = static_cast<int>(m_recClock.elapsed() / 1000.0 * m_recFps) + 1;
    const int n = std::clamp(due - m_recFrames, 1, m_recFps);
    const QByteArray frame(reinterpret_cast<const char *>(img.constBits()), static_cast<int>(img.sizeInBytes()));
    for (int i = 0; i < n; ++i) {
        if (m_recProc->write(frame) != frame.size()) {
            ++m_recDropped;
            return;
        }
    }
    m_recFrames += n;
    m_recRepeated += n - 1;
    // Don't let an ffmpeg that can't keep up buffer the whole clip in memory
    if (m_recProc->bytesToWrite() > 256 * 1024 * 1024)
        m_recProc->waitForBytesWritten(2000);
}

void UiDriver::recordStop()
{
    m_recTimer.stop();
    QProcess *proc = m_recProc;
    m_recProc = nullptr;
    if (!proc)
        return;
    // Close stdin and let ffmpeg write the moov atom
    proc->closeWriteChannel();
    if (!proc->waitForFinished(60000)) {
        m_recError = QStringLiteral("ffmpeg did not finish");
        proc->kill();
        proc->waitForFinished(2000);
    } else if (proc->exitStatus() != QProcess::NormalExit || proc->exitCode() != 0) {
        const QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed().right(300);
        m_recError = err.isEmpty() ? QStringLiteral("ffmpeg exit %1").arg(proc->exitCode()) : err;
    }
    delete proc;
}

QVariantMap UiDriver::recordStatus() const
{
    return {
        {QStringLiteral("recording"), m_recProc != nullptr},
        {QStringLiteral("path"), m_recPath},
        {QStringLiteral("fps"), m_recFps},
        {QStringLiteral("size"), QVariantList{m_recSize.width(), m_recSize.height()}},
        {QStringLiteral("frames"), m_recFrames},
        {QStringLiteral("dropped"), m_recDropped},
        {QStringLiteral("repeated"), m_recRepeated},
        {QStringLiteral("seconds"), m_recFps ? std::round(m_recFrames * 100.0 / m_recFps) / 100.0 : 0.0},
        {QStringLiteral("error"), m_recError},
    };
}

// ---------------------------------------------------------------------------
// Introspection + synthetic input
// ---------------------------------------------------------------------------

QVariantMap UiDriver::itemGeometry(QQuickItem *item)
{
    const QPointF p = item->mapToScene(QPointF(0, 0));
    // Effective visibility walks the parent chain (closed Popup content has
    // visible=false on an ancestor)
    bool visible = true;
    QVariant hiddenBy;
    for (QQuickItem *node = item; node; node = node->parentItem()) {
        if (!node->isVisible()) {
            visible = false;
            hiddenBy = QStringLiteral("%1#%2").arg(QString::fromLatin1(node->metaObject()->className()),
                                                   node->objectName().isEmpty() ? QStringLiteral("?") : node->objectName());
            break;
        }
    }
    return {
        {QStringLiteral("x"), p.x()},
        {QStringLiteral("y"), p.y()},
        {QStringLiteral("width"), item->width()},
        {QStringLiteral("height"), item->height()},
        {QStringLiteral("cx"), p.x() + item->width() / 2},
        {QStringLiteral("cy"), p.y() + item->height() / 2},
        {QStringLiteral("visible"), visible},
        {QStringLiteral("opacity"), item->opacity()},
        {QStringLiteral("hiddenBy"), hiddenBy},
    };
}

QObject *UiDriver::findObject(const QString &name) const
{
    QQuickWindow *w = window();
    if (!w)
        return nullptr;
    if (name == QLatin1String("root"))
        return w;   // the ApplicationWindow (Main.qml properties such as shiftLightEnabled)
    QQuickItem *first = nullptr;
    QQuickItem *visible = nullptr;
    walk(w->contentItem(), [&](QQuickItem *it) {
        if (it->objectName() != name)
            return false;
        if (itemGeometry(it).value(QStringLiteral("visible")).toBool()) {
            visible = it;
            return true;
        }
        if (!first)
            first = it;
        return false;
    });
    return visible ? visible : first;
}

QVariant UiDriver::findItem(const QString &name) const
{
    QObject *obj = findObject(name);
    if (auto *item = qobject_cast<QQuickItem *>(obj))
        return itemGeometry(item);
    if (auto *w = qobject_cast<QQuickWindow *>(obj))
        return itemGeometry(w->contentItem());
    return QVariant();
}

QVariant UiDriver::findText(const QString &text, const QString &prop) const
{
    QQuickWindow *w = window();
    if (!w)
        return QVariant();
    const QString want = text.trimmed().toLower();
    const QByteArray property = (prop.isEmpty() ? QStringLiteral("text") : prop).toUtf8();
    QVariant found;
    walk(w->contentItem(), [&](QQuickItem *it) {
        const QVariant v = it->property(property.constData());
        if (v.metaType().id() != QMetaType::QString || v.toString().trimmed().toLower() != want)
            return false;
        const QVariantMap g = itemGeometry(it);
        if (g.value(QStringLiteral("visible")).toBool() && g.value(QStringLiteral("opacity")).toDouble() > 0) {
            found = g;
            return true;
        }
        return false;
    });
    return found;
}

QVariantList UiDriver::listItems(const QString &prefix) const
{
    QVariantList out;
    QQuickWindow *w = window();
    if (!w)
        return out;
    walk(w->contentItem(), [&](QQuickItem *it) {
        const QString n = it->objectName();
        if (!n.isEmpty() && n.startsWith(prefix)) {
            QVariantMap g = itemGeometry(it);
            g.insert(QStringLiteral("name"), n);
            out.append(g);
        }
        return false;
    });
    return out;
}

QVariant UiDriver::getProperty(const QString &name, const QString &prop) const
{
    QObject *obj = findObject(name);
    return obj ? obj->property(prop.toUtf8().constData()) : QVariant();
}

bool UiDriver::setProperty(const QString &name, const QString &prop, const QVariant &value)
{
    QObject *obj = findObject(name);
    return obj && obj->setProperty(prop.toUtf8().constData(), value);
}

bool UiDriver::callMethod(const QString &name, const QString &method)
{
    QObject *obj = findObject(name);
    if (!obj)
        return false;
    try {
        invokeByName(obj, method.toUtf8().constData());
        return true;
    } catch (const CommandError &) {
        return false;
    }
}

void UiDriver::mouse(int kind, double x, double y)
{
    QQuickWindow *w = window();
    if (!w)
        return;
    // QTest injects through QWindowSystemInterface, so the window's delivery
    // agent keeps per-device grab state across press / move / release exactly
    // as for a real finger. Hand-built QMouseEvents look like a fresh press to
    // pointer handlers (DragHandler, TapHandler) on every move.
    const QPoint p(qRound(x), qRound(y));
    if (kind == QEvent::MouseButtonPress)
        QTest::mousePress(w, Qt::LeftButton, Qt::NoModifier, p);
    else if (kind == QEvent::MouseButtonRelease)
        QTest::mouseRelease(w, Qt::LeftButton, Qt::NoModifier, p);
    else
        QTest::mouseMove(w, p);
}

void UiDriver::click(double x, double y)
{
    mouse(QEvent::MouseButtonPress, x, y);
    mouse(QEvent::MouseButtonRelease, x, y);
}

void UiDriver::longPress(double x, double y, int ms)
{
    // The event loop keeps running while held, so pressAndHold timers fire
    mouse(QEvent::MouseButtonPress, x, y);
    QTest::qWait(std::max(0, ms));
    mouse(QEvent::MouseButtonRelease, x, y);
}

void UiDriver::drag(double x1, double y1, double x2, double y2, int steps)
{
    mouse(QEvent::MouseButtonPress, x1, y1);
    steps = std::max(1, steps);
    for (int i = 1; i <= steps; ++i) {
        const double t = double(i) / steps;
        mouse(QEvent::MouseMove, x1 + (x2 - x1) * t, y1 + (y2 - y1) * t);
        QCoreApplication::processEvents();
    }
    mouse(QEvent::MouseButtonRelease, x2, y2);
}

void UiDriver::wheel(double x, double y, int notches)
{
    QQuickWindow *w = window();
    if (!w)
        return;
    // One 120-unit event per mouse-wheel notch; positive scrolls up / away.
    const QPointF p(x, y);
    for (int i = 0; i < std::abs(notches); ++i) {
        QWheelEvent ev(p, w->mapToGlobal(p), QPoint(), QPoint(0, notches > 0 ? 120 : -120), Qt::NoButton,
                       Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(w, &ev);
        QCoreApplication::processEvents();
    }
}

QVariant UiDriver::typeText(const QString &text, bool enter, const QString &field)
{
    QQuickWindow *w = window();
    if (!w)
        return QVariant();
    // Keys go straight to the item: a headless or inactive window drops
    // window-level key delivery, and dev mode's keyboard handler holds focus
    QQuickItem *target = nullptr;
    if (!field.isEmpty()) {
        const QString want = field.trimmed().toLower();
        walk(w->contentItem(), [&](QQuickItem *it) {
            const QVariant ph = it->property("placeholderText");
            const bool match = it->objectName() == field
                               || (ph.metaType().id() == QMetaType::QString && ph.toString().trimmed().toLower() == want);
            if (match && itemGeometry(it).value(QStringLiteral("visible")).toBool()) {
                target = it;
                return true;
            }
            return false;
        });
        if (target)
            target->forceActiveFocus();
    } else {
        target = w->activeFocusItem();
    }
    if (!target)
        return QVariant();
    for (const QChar ch : text) {
        for (QEvent::Type t : {QEvent::KeyPress, QEvent::KeyRelease}) {
            QKeyEvent ev(t, 0, Qt::NoModifier, QString(ch));
            QCoreApplication::sendEvent(target, &ev);
        }
    }
    if (enter) {
        for (QEvent::Type t : {QEvent::KeyPress, QEvent::KeyRelease}) {
            QKeyEvent ev(t, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
            QCoreApplication::sendEvent(target, &ev);
        }
    }
    return QVariantMap{
        {QStringLiteral("focus"), QString::fromLatin1(target->metaObject()->className())},
        {QStringLiteral("text"), target->property("text")},
    };
}

} // namespace OctaveDev
