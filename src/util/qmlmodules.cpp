// qmlmodules.cpp

#include "qmlmodules.h"

#include <QFile>
#include <QQmlEngine>
#include <QUrl>

namespace QmlModules {

bool isInstalled(const QQmlEngine &engine, const QString &uri)
{
    const QString qmldir = QString(uri).replace(QLatin1Char('.'), QLatin1Char('/')) + QStringLiteral("/qmldir");
    for (QString path : engine.importPathList()) {
        if (path.startsWith(QLatin1String("qrc:")))
            path = QLatin1Char(':') + QUrl(path).path();   // "qrc:/x", "qrc:///x" -> ":/x"
        if (QFile::exists(path + QLatin1Char('/') + qmldir))
            return true;
    }
    return false;
}

} // namespace QmlModules
