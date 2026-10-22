// qmlmodules.h
//
// Whether an optional QML module (QtQuick3D, its AssetUtils) is installed,
// answered from its qmldir in the engine's import paths: a few file checks,
// no plugin load. Found at build time is not installed at run time for a
// distro build, and the pages that need these modules load them only when
// opened, so main.cpp asks this before engine.load and publishes the answers
// as context properties (hasQuick3D, hasQuick3DAssetUtils).

#ifndef QMLMODULES_H
#define QMLMODULES_H

#include <QString>

class QQmlEngine;

namespace QmlModules {

// `uri` as imported, e.g. "QtQuick3D.AssetUtils"
bool isInstalled(const QQmlEngine &engine, const QString &uri);

} // namespace QmlModules

#endif // QMLMODULES_H
