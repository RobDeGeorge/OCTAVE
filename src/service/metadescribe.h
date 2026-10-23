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
//
// API annotations (docs/api/README.md, Versioning), declared next to the
// members with Q_CLASSINFO in the class itself:
//   Q_CLASSINFO("octave.experimental", "levels setDemand")   // not covered by the protocol version
//   Q_CLASSINFO("octave.experimental", "*")                  // the whole object
//   Q_CLASSINFO("octave.deprecated.oldName", "use newName; goes in protocol 3")
// They show as "stability": "experimental" and "deprecated": "..." on the
// member (or "stability" on the object). Every member is stable unless
// marked; a method name covers all its overloads.

#ifndef OCTAVE_SERVICE_METADESCRIBE_H
#define OCTAVE_SERVICE_METADESCRIBE_H

#include <QStringList>
#include <QVariantMap>

class QMetaObject;
class QObject;

namespace OctaveService {

// {class, properties: [...], methods: [...], signals: [...]}
QVariantMap describeMetaObject(const QMetaObject *mo);
inline QVariantMap describeObject(const QObject *object);
// Annotations that name a member the class does not offer, or deprecate one
// without a replacement; --dump-api refuses to write docs/api/ while any exist
QStringList annotationErrors(const QMetaObject *mo);

} // namespace OctaveService

#include <QObject>

inline QVariantMap OctaveService::describeObject(const QObject *object)
{
    return object ? describeMetaObject(object->metaObject()) : QVariantMap();
}

#endif // OCTAVE_SERVICE_METADESCRIBE_H
