#include "collagebridge.h"

#include "../service/commandserver.h"

#include <QFile>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSet>
#include <QtQml/qqml.h>

#include <algorithm>

namespace OctaveDev {

namespace {

// Popup subclasses that aren't "screens" -- they'd flood the collage
const char *const kSkipPopupTypes[] = {"QQuickToolTip"};
const char *const kSkipPopupOwners[] = {"QQuickComboBox"};

const QByteArray kHostQml = R"(
import QtQuick
Rectangle {
    objectName: "collageHost"
    anchors.fill: parent
    z: 1000000
    color: "#0b0d10"
    // Swallow input so the app underneath can't react to anything.
    MouseArea { anchors.fill: parent; hoverEnabled: true }
}
)";

bool inheritsClass(const QObject *obj, const char *className)
{
    for (const QMetaObject *mo = obj ? obj->metaObject() : nullptr; mo; mo = mo->superClass()) {
        if (qstrcmp(mo->className(), className) == 0)
            return true;
    }
    return false;
}

// Defaults for `required property <type> <name>` so components that demand
// inputs still render something
QVariant requiredDefault(const QString &type)
{
    static const QHash<QString, QVariant> defaults = {
        {"string", QStringLiteral("Sample")}, {"url", QString()}, {"int", 1}, {"real", 0.5}, {"double", 0.5},
        {"bool", false}, {"color", QStringLiteral("#808080")}, {"list", QVariantList()},
    };
    return defaults.value(type.section(QLatin1Char('<'), 0, 0));
}

QStringList errorStrings(const QList<QQmlError> &errors)
{
    QStringList out;
    for (const QQmlError &e : errors)
        out << e.toString();
    return out;
}

} // namespace

CollageBridge::CollageBridge(QQmlApplicationEngine *engine, const QDir &projectRoot, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_root(projectRoot)
{
    connect(engine, &QQmlEngine::warnings, this, [this](const QList<QQmlError> &errors) {
        m_warnings << errorStrings(errors);
        if (m_warnings.size() > 200)
            m_warnings = m_warnings.mid(m_warnings.size() - 200);
    });
}

void CollageBridge::registerActions(CommandServer &server)
{
    QPointer<CollageBridge> self = this;
    auto onGui = [self](std::function<QVariant(CollageBridge *)> fn) {
        return CommandServer::onGui([self, fn] {
            if (!self)
                throw CommandError(QStringLiteral("collage bridge gone"));
            return fn(self.data());
        });
    };
    server.registerAction("collage_list_popups", [onGui](const QVariantMap &) {
        return onGui([](CollageBridge *b) { return b->listPopups(); });
    });
    server.registerAction("collage_open_popup", [onGui](const QVariantMap &a) {
        const int index = a.value("index").toInt();
        return onGui([index](CollageBridge *b) { return b->openPopup(index); });
    });
    server.registerAction("collage_close_popups", [onGui](const QVariantMap &) {
        return onGui([](CollageBridge *b) { return b->closePopups(); });
    });
    server.registerAction("collage_mount", [onGui](const QVariantMap &a) {
        const QString file = a.value("file").toString();
        const bool fill = a.value("fill").toBool();
        const QVariantList size = a.value("default_size", QVariantList{480, 360}).toList();
        const double w = size.value(0, 480).toDouble(), h = size.value(1, 360).toDouble();
        return onGui([=](CollageBridge *b) { return b->mount(file, fill, w, h); });
    });
    server.registerAction("collage_grab", [onGui](const QVariantMap &a) {
        const QString path = a.value("path").toString();
        return onGui([path](CollageBridge *b) { return b->grab(path); });
    });
    server.registerAction("collage_unmount", [onGui](const QVariantMap &) {
        return onGui([](CollageBridge *b) { return b->unmount(); });
    });
}

QString CollageBridge::sourceFile(QObject *obj) const
{
    for (QQmlContext *ctx = qmlContext(obj); ctx; ctx = ctx->parentContext()) {
        const QUrl url = ctx->baseUrl();
        if (url.isValid() && !url.isEmpty()) {
            const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
            return QDir::isAbsolutePath(path) ? m_root.relativeFilePath(path) : path;
        }
    }
    return QString();
}

QVariant CollageBridge::listPopups()
{
    const auto roots = m_engine->rootObjects();
    auto *win = roots.isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(roots.first());
    m_popups.clear();
    if (!win)
        return QVariantList();
    QSet<QObject *> seen;
    for (QObject *obj : win->findChildren<QObject *>()) {
        if (seen.contains(obj) || !inheritsClass(obj, "QQuickPopup"))
            continue;
        seen.insert(obj);
        if (std::any_of(std::begin(kSkipPopupTypes), std::end(kSkipPopupTypes),
                        [obj](const char *t) { return inheritsClass(obj, t); }))
            continue;
        QObject *owner = obj->parent();
        if (owner && std::any_of(std::begin(kSkipPopupOwners), std::end(kSkipPopupOwners),
                                 [owner](const char *t) { return inheritsClass(owner, t); }))
            continue;
        if (obj->objectName() == QLatin1String("devOverlay") || sourceFile(obj).contains(QLatin1String("dev/")))
            continue;
        m_popups.append(obj);
    }
    QVariantList out;
    for (int i = 0; i < m_popups.size(); ++i) {
        QObject *p = m_popups.at(i);
        out.append(QVariantMap{
            {"index", i},
            {"objectName", p->objectName()},
            {"type", QString::fromLatin1(p->metaObject()->className())},
            {"file", sourceFile(p)},
            {"title", p->property("title").toString()},
            {"visible", p->property("visible").toBool()},
        });
    }
    return out;
}

QVariant CollageBridge::openPopup(int index)
{
    if (index < 0 || index >= m_popups.size() || !m_popups.at(index))
        throw CommandError(QStringLiteral("no popup %1 in the last listing").arg(index));
    QObject *p = m_popups.at(index);
    m_warnings.clear();
    try {
        invokeByName(p, "open");
    } catch (const CommandError &) {
        p->setProperty("visible", true);
    }
    m_opened.append(p);
    return QVariantMap{{"opened", true}};
}

QVariant CollageBridge::closePopups()
{
    for (const QPointer<QObject> &p : std::as_const(m_opened)) {
        if (!p)
            continue;   // already destroyed with its page
        try {
            invokeByName(p, "close");
        } catch (const CommandError &) {
            p->setProperty("visible", false);
        }
    }
    m_opened.clear();
    return QVariantMap{{"closed", true}};
}

QQuickItem *CollageBridge::ensureHost()
{
    const auto roots = m_engine->rootObjects();
    auto *win = roots.isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(roots.first());
    if (!win)
        throw CommandError(QStringLiteral("no window"));
    if (!m_host) {
        QQmlComponent comp(m_engine);
        comp.setData(kHostQml, QUrl::fromLocalFile(m_root.filePath(QStringLiteral("src/dev/qml/CollageHost.qml"))));
        QObject *obj = comp.create(m_engine->rootContext());
        auto *host = qobject_cast<QQuickItem *>(obj);
        if (!host) {
            delete obj;
            throw CommandError(QStringLiteral("could not create host: %1").arg(errorStrings(comp.errors()).join("; ")));
        }
        QQmlEngine::setObjectOwnership(host, QQmlEngine::CppOwnership);
        host->setParent(win);
        host->setParentItem(win->contentItem());
        m_host = host;
    }
    m_host->setVisible(true);
    return m_host;
}

QVariant CollageBridge::mount(const QString &file, bool fill, double defaultW, double defaultH)
{
    unmount();
    const auto roots = m_engine->rootObjects();
    auto *win = roots.isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(roots.first());
    if (!win)
        throw CommandError(QStringLiteral("no window"));
    const QString path = m_root.filePath(file);
    m_warnings.clear();

    QQmlComponent comp(m_engine, QUrl::fromLocalFile(path), QQmlComponent::PreferSynchronous);
    if (comp.isError())
        return QVariantMap{{"ok", false}, {"errors", errorStrings(comp.errors())}};

    QVariantMap initial;
    QFile src(path);
    if (src.open(QIODevice::ReadOnly | QIODevice::Text)) {
        static const QRegularExpression requiredRe(QStringLiteral("^\\s*required\\s+property\\s+([\\w.<>]+)\\s+(\\w+)"),
                                                   QRegularExpression::MultilineOption);
        auto it = requiredRe.globalMatch(QString::fromUtf8(src.readAll()));
        while (it.hasNext()) {
            const auto m = it.next();
            initial.insert(m.captured(2), requiredDefault(m.captured(1)));
        }
    }

    QQuickItem *host = ensureHost();
    QQmlContext *ctx = m_engine->rootContext();
    QObject *obj = initial.isEmpty() ? comp.create(ctx) : comp.createWithInitialProperties(initial, ctx);
    if (!obj) {
        host->setVisible(false);
        QStringList errors = errorStrings(comp.errors()) + m_warnings.mid(0, 20);
        return QVariantMap{{"ok", false}, {"errors", errors}};
    }
    QQmlEngine::setObjectOwnership(obj, QQmlEngine::CppOwnership);
    obj->setParent(host);
    m_mounted = obj;

    if (inheritsClass(obj, "QQuickPopup")) {
        m_mountedIsPopup = true;
        obj->setProperty("parent", QVariant::fromValue(win->contentItem()));
        obj->setProperty("z", 1000001);
        invokeByName(obj, "open");
        return QVariantMap{{"ok", true}, {"kind", "popup"}, {"required_defaults", QStringList(initial.keys())}};
    }
    auto *item = qobject_cast<QQuickItem *>(obj);
    if (!item) {
        const QString cls = QString::fromLatin1(obj->metaObject()->className());
        unmount();
        return QVariantMap{{"ok", false}, {"nonvisual", true},
                           {"errors", QStringList{QStringLiteral("%1 is not a visual item").arg(cls)}}};
    }

    m_mountedIsPopup = false;
    item->setParentItem(host);
    const double W = host->width(), H = host->height();
    const double iw = item->implicitWidth(), ih = item->implicitHeight();
    double w = W, h = H;
    if (!fill) {
        w = std::min(iw > 0 ? iw : defaultW, W - 32);
        h = std::min(ih > 0 ? ih : defaultH, H - 32);
    }
    item->setSize(QSizeF(w, h));
    item->setPosition(fill ? QPointF(0, 0) : QPointF((W - w) / 2, (H - h) / 2));
    item->setVisible(true);
    return QVariantMap{{"ok", true}, {"kind", "item"}, {"size", QVariantList{w, h}},
                       {"implicit", QVariantList{iw, ih}}, {"required_defaults", QStringList(initial.keys())}};
}

QVariant CollageBridge::grab(const QString &path)
{
    const auto roots = m_engine->rootObjects();
    auto *win = roots.isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(roots.first());
    if (!win)
        throw CommandError(QStringLiteral("no window"));
    QImage img = win->grabWindow();
    if (img.isNull())
        throw CommandError(QStringLiteral("grabWindow returned nothing"));
    auto *item = qobject_cast<QQuickItem *>(m_mounted.data());
    if (item && !m_mountedIsPopup) {
        // Crop to the item (plus a little breathing room), in device pixels
        const double dpr = img.devicePixelRatio() > 0 ? img.devicePixelRatio() : 1.0;
        constexpr double pad = 16;
        const QRectF r = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()))
                             .adjusted(-pad, -pad, pad, pad)
                             .intersected(QRectF(0, 0, win->width(), win->height()));
        img = img.copy(int(r.x() * dpr), int(r.y() * dpr), int(r.width() * dpr), int(r.height() * dpr));
    }
    const bool ok = img.save(path, "PNG");
    QStringList warnings = m_warnings;
    warnings.removeDuplicates();
    return QVariantMap{{"saved", ok}, {"warnings", warnings.mid(0, 20)}};
}

QVariant CollageBridge::unmount()
{
    QObject *obj = m_mounted;
    m_mounted = nullptr;
    if (obj) {
        if (m_mountedIsPopup) {
            try {
                invokeByName(obj, "close");
            } catch (const CommandError &) {
            }
        }
        if (auto *item = qobject_cast<QQuickItem *>(obj)) {
            item->setVisible(false);
            item->setParentItem(nullptr);
        }
        obj->deleteLater();
    }
    if (m_host)
        m_host->setVisible(false);
    m_mountedIsPopup = false;
    return QVariantMap{{"unmounted", true}};
}

} // namespace OctaveDev
