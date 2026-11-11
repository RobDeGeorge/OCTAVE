#include "releasecheck.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QRegularExpression>

#include <climits>

namespace {

struct Parsed {
    QList<int> parts;
    int pre = INT_MAX;   // INT_MAX: a release; else the number of its pre-release ("rc2" = 2)
};

Parsed parse(const QString &text)
{
    QString s = text.trimmed();
    if (s.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
        s.remove(0, 1);
    Parsed p;
    const int dash = s.indexOf(QLatin1Char('-'));
    if (dash >= 0) {
        static const QRegularExpression digits(QStringLiteral("(\\d+)"));
        const auto m = digits.match(s.mid(dash + 1));
        p.pre = m.hasMatch() ? m.captured(1).toInt() : 0;
        s.truncate(dash);
    }
    const auto fields = s.split(QLatin1Char('.'));
    for (const QString &f : fields)
        p.parts.append(f.toInt());   // not a number: 0
    return p;
}

} // namespace

namespace ReleaseCheck {

int compareVersions(const QString &a, const QString &b)
{
    const Parsed pa = parse(a);
    const Parsed pb = parse(b);
    const int n = qMax(pa.parts.size(), pb.parts.size());
    for (int i = 0; i < n; ++i) {
        const int x = pa.parts.value(i, 0);
        const int y = pb.parts.value(i, 0);
        if (x != y)
            return x < y ? -1 : 1;
    }
    if (pa.pre != pb.pre)
        return pa.pre < pb.pre ? -1 : 1;
    return 0;
}

Release parseLatest(const QByteArray &json, const QString &repo)
{
    Release r;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
        return r;
    const QJsonObject o = doc.object();
    if (o.value(QStringLiteral("draft")).toBool() || o.value(QStringLiteral("prerelease")).toBool())
        return r;
    const QString tag = o.value(QStringLiteral("tag_name")).toString().trimmed();
    static const QRegularExpression versionTag(QStringLiteral("^[vV]?\\d+(\\.\\d+)*(-[A-Za-z0-9.]+)?$"));
    if (!versionTag.match(tag).hasMatch())
        return r;

    r.valid = true;
    r.tag = tag;
    r.version = tag.startsWith(QLatin1Char('v'), Qt::CaseInsensitive) ? tag.mid(1) : tag;
    r.name = o.value(QStringLiteral("name")).toString().trimmed();
    const QString releases = QStringLiteral("https://github.com/%1/releases").arg(repo);
    const QString page = o.value(QStringLiteral("html_url")).toString();
    r.url = page.startsWith(releases + QLatin1Char('/')) ? page : releases;
    return r;
}

} // namespace ReleaseCheck
