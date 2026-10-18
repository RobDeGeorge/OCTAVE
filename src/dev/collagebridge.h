#ifndef OCTAVE_DEV_COLLAGEBRIDGE_H
#define OCTAVE_DEV_COLLAGEBRIDGE_H

// In-app half of the UI collage tool (C++ port of dev/collage/bridge.py).
// The driver, dev/collage/__main__.py, calls these command-server actions:
//
//   collage_list_popups   every Popup/Dialog/Drawer/Menu currently in the window
//   collage_open_popup    open one of them (by index from the last list)
//   collage_close_popups  close everything collage_open_popup opened
//   collage_mount         instantiate any .qml file in isolation on a host
//                         surface layered over the app (popups get open()ed)
//   collage_grab          grab the window, cropped to the mounted thing
//   collage_unmount       destroy it
//
// Every method runs on the GUI thread.

#include <QDir>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariant>

class QQmlApplicationEngine;
class QQuickItem;

namespace OctaveDev {

class CommandServer;

class CollageBridge : public QObject
{
    Q_OBJECT

public:
    CollageBridge(QQmlApplicationEngine *engine, const QDir &projectRoot, QObject *parent = nullptr);

    void registerActions(CommandServer &server);

    QVariant listPopups();
    QVariant openPopup(int index);
    QVariant closePopups();
    QVariant mount(const QString &file, bool fill, double defaultW, double defaultH);
    QVariant grab(const QString &path);
    QVariant unmount();

private:
    QQuickItem *ensureHost();
    QString sourceFile(QObject *obj) const;

    QQmlApplicationEngine *m_engine;
    QDir m_root;
    QList<QPointer<QObject>> m_popups;   // last listing
    QList<QPointer<QObject>> m_opened;   // popups we opened, to close again
    QPointer<QQuickItem> m_host;
    QPointer<QObject> m_mounted;
    bool m_mountedIsPopup = false;
    QStringList m_warnings;
};

} // namespace OctaveDev

#endif // OCTAVE_DEV_COLLAGEBRIDGE_H
