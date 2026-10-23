// metadescribe.cpp

#include "metadescribe.h"
#include "memberscope.h"

#include <QHash>
#include <QMetaClassInfo>
#include <QMetaEnum>
#include <QMetaMethod>
#include <QMetaObject>
#include <QMetaProperty>
#include <QSet>

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

constexpr char kExperimentalKey[] = "octave.experimental";
constexpr char kDeprecatedPrefix[] = "octave.deprecated.";

// The API annotations the app's own classes declare (Q_CLASSINFO, see the header)
struct Annotations
{
    bool objectExperimental = false;
    QSet<QString> experimental;
    QHash<QString, QString> deprecated;
};

Annotations annotationsOf(const QMetaObject *mo)
{
    Annotations a;
    const QMetaObject *qtBase = firstQtBase(mo);
    for (int i = qtBase ? qtBase->classInfoCount() : 0; i < mo->classInfoCount(); ++i) {
        const QMetaClassInfo info = mo->classInfo(i);
        const QByteArray key(info.name());
        if (key == kExperimentalKey) {
            for (const QString &name : QString::fromUtf8(info.value()).split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
                if (name == QLatin1String("*"))
                    a.objectExperimental = true;
                else
                    a.experimental.insert(name);
            }
        } else if (key.startsWith(kDeprecatedPrefix)) {
            a.deprecated.insert(QString::fromUtf8(key.mid(int(sizeof(kDeprecatedPrefix)) - 1)),
                                QString::fromUtf8(info.value()));
        }
    }
    return a;
}

void annotate(QVariantList &members, const Annotations &a)
{
    for (QVariant &member : members) {
        QVariantMap m = member.toMap();
        const QString name = m.value(QStringLiteral("name")).toString();
        if (a.experimental.contains(name))
            m.insert(QStringLiteral("stability"), QStringLiteral("experimental"));
        if (a.deprecated.contains(name))
            m.insert(QStringLiteral("deprecated"), a.deprecated.value(name));
        member = m;
    }
}

} // namespace

QStringList annotationErrors(const QMetaObject *mo)
{
    if (!mo)
        return {};
    const Annotations a = annotationsOf(mo);
    QSet<QString> members;
    const QVariantMap d = describeMetaObject(mo);
    for (const char *list : {"properties", "methods", "signals"})
        for (const QVariant &m : d.value(QString::fromLatin1(list)).toList())
            members.insert(m.toMap().value(QStringLiteral("name")).toString());
    QStringList errors;
    QStringList named(a.experimental.cbegin(), a.experimental.cend());
    named += a.deprecated.keys();
    for (const QString &name : std::as_const(named))
        if (!members.contains(name))
            errors.append(QStringLiteral("%1: annotation names %2, which it does not offer").arg(
                QString::fromLatin1(mo->className()), name));
    for (auto it = a.deprecated.cbegin(); it != a.deprecated.cend(); ++it)
        if (it.value().trimmed().isEmpty())
            errors.append(QStringLiteral("%1: %2 is deprecated without saying what to use instead").arg(
                QString::fromLatin1(mo->className()), it.key()));
    errors.sort();
    return errors;
}

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

    const Annotations a = annotationsOf(mo);
    annotate(properties, a);
    annotate(methods, a);
    annotate(signalList, a);

    QVariantMap out{{QStringLiteral("class"), QString::fromLatin1(mo->className())},
                    {QStringLiteral("properties"), properties},
                    {QStringLiteral("methods"), methods},
                    {QStringLiteral("signals"), signalList}};
    if (!enums.isEmpty())
        out.insert(QStringLiteral("enums"), enums);
    if (a.objectExperimental)
        out.insert(QStringLiteral("stability"), QStringLiteral("experimental"));
    return out;
}

} // namespace OctaveService
