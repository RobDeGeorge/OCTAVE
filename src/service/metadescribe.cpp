// metadescribe.cpp

#include "metadescribe.h"
#include "memberscope.h"

#include <QMetaEnum>
#include <QMetaMethod>
#include <QMetaObject>
#include <QMetaProperty>

namespace OctaveService {

namespace {

using MemberScope::firstQtBase;

QString typeName(QMetaType type)
{
    return type.isValid() ? QString::fromLatin1(type.name()) : QString();
}

QVariantList enumKeys(const QMetaEnum &e)
{
    QVariantList keys;
    for (int k = 0; k < e.keyCount(); ++k)
        keys.append(QString::fromLatin1(e.key(k)));
    return keys;
}

QVariantMap describeMethod(const QMetaMethod &m)
{
    QVariantList params;
    const QList<QByteArray> names = m.parameterNames();
    for (int p = 0; p < m.parameterCount(); ++p)
        params.append(QVariantMap{{QStringLiteral("name"), QString::fromLatin1(names.value(p))},
                                  {QStringLiteral("type"), typeName(m.parameterMetaType(p))}});
    QVariantMap out{{QStringLiteral("name"), QString::fromLatin1(m.name())},
                    {QStringLiteral("params"), params}};
    if (m.methodType() != QMetaMethod::Signal) {
        out.insert(QStringLiteral("kind"), m.methodType() == QMetaMethod::Slot ? QStringLiteral("slot")
                                                                               : QStringLiteral("invokable"));
        const QMetaType rt = m.returnMetaType();
        out.insert(QStringLiteral("returns"), rt.id() == QMetaType::Void ? QString() : typeName(rt));
    }
    return out;
}

} // namespace

QVariantMap describeMetaObject(const QMetaObject *mo)
{
    if (!mo)
        return {};
    const QMetaObject *qtBase = firstQtBase(mo);
    const int propStart = qtBase ? qtBase->propertyCount() : 0;
    const int methodStart = qtBase ? qtBase->methodCount() : 0;

    QVariantList properties;
    for (int i = propStart; i < mo->propertyCount(); ++i) {
        const QMetaProperty p = mo->property(i);
        QVariantMap prop{{QStringLiteral("name"), QString::fromLatin1(p.name())},
                         {QStringLiteral("type"), typeName(p.metaType())},
                         {QStringLiteral("readable"), p.isReadable()},
                         {QStringLiteral("writable"), p.isWritable()},
                         {QStringLiteral("constant"), p.isConstant()}};
        if (p.hasNotifySignal())
            prop.insert(QStringLiteral("notify"), QString::fromLatin1(p.notifySignal().name()));
        if (p.isEnumType())
            prop.insert(QStringLiteral("enum"), enumKeys(p.enumerator()));
        properties.append(prop);
    }

    QVariantList methods, signalList;
    for (int i = methodStart; i < mo->methodCount(); ++i) {
        const QMetaMethod m = mo->method(i);
        if (m.access() != QMetaMethod::Public)
            continue;
        if (m.methodType() == QMetaMethod::Signal)
            signalList.append(describeMethod(m));
        else if (m.methodType() == QMetaMethod::Slot || m.methodType() == QMetaMethod::Method)
            methods.append(describeMethod(m));
    }

    QVariantMap enums;
    for (int i = qtBase ? qtBase->enumeratorCount() : 0; i < mo->enumeratorCount(); ++i)
        enums.insert(QString::fromLatin1(mo->enumerator(i).name()), enumKeys(mo->enumerator(i)));

    QVariantMap out{{QStringLiteral("class"), QString::fromLatin1(mo->className())},
                    {QStringLiteral("properties"), properties},
                    {QStringLiteral("methods"), methods},
                    {QStringLiteral("signals"), signalList}};
    if (!enums.isEmpty())
        out.insert(QStringLiteral("enums"), enums);
    return out;
}

} // namespace OctaveService
