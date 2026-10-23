// metadescribe.cpp

#include "metadescribe.h"
#include "memberscope.h"
#include "servicescopes.h"

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
constexpr char kScopeKey[] = "octave.scope";
constexpr char kScopePrefix[] = "octave.scope.";

// The API annotations the app's own classes declare (Q_CLASSINFO, see the header)
struct Annotations
{
    bool objectExperimental = false;
    QSet<QString> experimental;
    QHash<QString, QString> deprecated;
    QString defaultScope;                  // octave.scope; "" = not declared
    QHash<QString, QString> memberScopes;  // octave.scope.<scope> lists
    QStringList scopeErrors;
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
        } else if (key == kScopeKey) {
            a.defaultScope = QString::fromUtf8(info.value()).trimmed();
            if (!knownScopes().contains(a.defaultScope))
                a.scopeErrors.append(QStringLiteral("unknown scope %1").arg(a.defaultScope));
        } else if (key.startsWith(kScopePrefix)) {
            const QString scope = QString::fromUtf8(key.mid(int(sizeof(kScopePrefix)) - 1));
            if (!knownScopes().contains(scope))
                a.scopeErrors.append(QStringLiteral("unknown scope %1").arg(scope));
            for (const QString &name : QString::fromUtf8(info.value()).split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
                if (a.memberScopes.contains(name) && a.memberScopes.value(name) != scope)
                    a.scopeErrors.append(QStringLiteral("%1 is in two scopes").arg(name));
                a.memberScopes.insert(name, scope);
            }
        } else if (key.startsWith(kDeprecatedPrefix)) {
            a.deprecated.insert(QString::fromUtf8(key.mid(int(sizeof(kDeprecatedPrefix)) - 1)),
                                QString::fromUtf8(info.value()));
        }
    }
    return a;
}

QString scopeIn(const Annotations &a, const QString &member)
{
    if (const auto it = a.memberScopes.constFind(member); it != a.memberScopes.cend())
        return it.value();
    return a.defaultScope.isEmpty() ? QStringLiteral("full") : a.defaultScope;
}

// `scoped`: what calling it (a method) or setting it (a writable property) needs
void annotate(QVariantList &members, const Annotations &a, bool scoped = false)
{
    for (QVariant &member : members) {
        QVariantMap m = member.toMap();
        const QString name = m.value(QStringLiteral("name")).toString();
        if (scoped && m.value(QStringLiteral("writable"), true).toBool())
            m.insert(QStringLiteral("scope"), scopeIn(a, name));
        if (a.experimental.contains(name))
            m.insert(QStringLiteral("stability"), QStringLiteral("experimental"));
        if (a.deprecated.contains(name))
            m.insert(QStringLiteral("deprecated"), a.deprecated.value(name));
        member = m;
    }
}

} // namespace


QString memberScope(const QMetaObject *mo, const QString &member)
{
    return mo ? scopeIn(annotationsOf(mo), member) : QStringLiteral("full");
}

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
    named += a.memberScopes.keys();
    for (const QString &e : a.scopeErrors)
        errors.append(QStringLiteral("%1: %2").arg(QString::fromLatin1(mo->className()), e));
    // Anything a client can change says what permission it needs
    bool changes = !d.value(QStringLiteral("methods")).toList().isEmpty();
    for (const QVariant &p : d.value(QStringLiteral("properties")).toList())
        changes = changes || p.toMap().value(QStringLiteral("writable")).toBool();
    if (changes && a.defaultScope.isEmpty())
        errors.append(QStringLiteral("%1: no Q_CLASSINFO(\"octave.scope\", ...) for its methods / writable properties")
                          .arg(QString::fromLatin1(mo->className())));
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
    annotate(properties, a, true);
    annotate(methods, a, true);
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
