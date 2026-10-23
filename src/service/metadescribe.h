// metadescribe.h
//
// What a QObject offers a frontend, read from its meta-object: properties
// (type, read / write, notify signal, constant, enum keys), invokables and
// public slots (parameters, return type), and signals. The `describe` op
// serves it and step 3 of TODO/open-backend.md writes it to docs/api/.
// Members of Qt's own base classes (QObject, QAbstractItemModel,
// QQmlPropertyMap, ...) are left out: only what the app's classes declare.
// "Qt's" means the first class up the chain whose name starts with "Q", so
// an app class named Q... would be taken for Qt's.

#ifndef OCTAVE_SERVICE_METADESCRIBE_H
#define OCTAVE_SERVICE_METADESCRIBE_H

#include <QVariantMap>

class QMetaObject;
class QObject;

namespace OctaveService {

// {class, properties: [...], methods: [...], signals: [...]}
QVariantMap describeMetaObject(const QMetaObject *mo);
inline QVariantMap describeObject(const QObject *object);

} // namespace OctaveService

#include <QObject>

inline QVariantMap OctaveService::describeObject(const QObject *object)
{
    return object ? describeMetaObject(object->metaObject()) : QVariantMap();
}

#endif // OCTAVE_SERVICE_METADESCRIBE_H
