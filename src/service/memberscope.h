// memberscope.h
//
// Which members of an object the service offers: those its own classes
// declare, below the first Qt base class (QObject, QQmlPropertyMap, ...).
// `describe` lists exactly these, and `call` / `get` / `set` / `subscribe`
// reach nothing else: no deleteLater or destroyed, no objectName, no
// signal called as a method. Header-only, for every target that links the
// command server.

#ifndef OCTAVE_SERVICE_MEMBERSCOPE_H
#define OCTAVE_SERVICE_MEMBERSCOPE_H

#include <QByteArray>
#include <QMetaMethod>
#include <QMetaObject>

namespace OctaveService::MemberScope {

// The first meta-object in the chain that belongs to Qt itself (its class
// name starts with "Q"): its members and everything above are not ours
inline const QMetaObject *firstQtBase(const QMetaObject *mo)
{
    while (mo && !QByteArray(mo->className()).startsWith('Q'))
        mo = mo->superClass();
    return mo;
}

inline int methodStart(const QMetaObject *mo)
{
    const QMetaObject *qt = firstQtBase(mo);
    return qt ? qt->methodCount() : 0;
}

inline int propertyStart(const QMetaObject *mo)
{
    const QMetaObject *qt = firstQtBase(mo);
    return qt ? qt->propertyCount() : 0;
}

// A method `call` may run: public, a slot or Q_INVOKABLE (never a signal),
// declared by the object's own classes
inline bool callable(const QMetaObject *mo, int index)
{
    if (index < methodStart(mo) || index >= mo->methodCount())
        return false;
    const QMetaMethod m = mo->method(index);
    return m.access() == QMetaMethod::Public
           && (m.methodType() == QMetaMethod::Slot || m.methodType() == QMetaMethod::Method);
}

} // namespace OctaveService::MemberScope

#endif // OCTAVE_SERVICE_MEMBERSCOPE_H
