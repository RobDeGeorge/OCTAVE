#include "custompids.h"

#include "settingsmanager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QVarLengthArray>

#include <algorithm>
#include <cmath>
#include <limits>

Q_LOGGING_CATEGORY(lcCustomPids, "octave.obd.custompids")

// ── Formula ──────────────────────────────────────────────────────────
//
//   or     := and ('|' and)*
//   and    := shift ('&' shift)*
//   shift  := sum (('<<' | '>>') sum)*
//   sum    := term (('+' | '-') term)*
//   term   := unary (('*' | '/') unary)*
//   unary  := '-' unary | primary
//   primary:= number | byte | '(' or ')' | 'SIGNED' '(' or ')' | '{' byte ':' digit '}'
//   byte   := A ... Z | AA ... ZZ          (A = first data byte, AA = the 27th)

class FormulaParser
{
public:
    explicit FormulaParser(const QString &text)
        : m_text(text.toUpper())
    {
    }

    bool run(PidFormula *out, QString *error)
    {
        parseOr();
        skipSpace();
        if (m_error.isEmpty() && m_pos < m_text.size())
            fail(QStringLiteral("unexpected '%1'").arg(m_text.at(m_pos)));
        if (m_error.isEmpty() && m_code.isEmpty())
            fail(QStringLiteral("empty formula"));
        if (!m_error.isEmpty()) {
            if (error)
                *error = m_error;
            return false;
        }
        out->m_code = m_code;
        out->m_bytes = m_bytes;
        return true;
    }

private:
    using Op = PidFormula::Op;

    void fail(const QString &why)
    {
        if (m_error.isEmpty())
            m_error = QStringLiteral("%1 at character %2").arg(why).arg(m_pos + 1);
    }
    void skipSpace()
    {
        while (m_pos < m_text.size() && m_text.at(m_pos).isSpace())
            ++m_pos;
    }
    bool take(const char *token)
    {
        skipSpace();
        const QLatin1String t(token);
        if (!QStringView(m_text).mid(m_pos).startsWith(t))
            return false;
        m_pos += t.size();
        return true;
    }
    void emitOp(Op op) { m_code.append({op, 0, 0, 0}); }

    void parseOr()
    {
        parseAnd();
        while (m_error.isEmpty() && take("|")) {
            parseAnd();
            emitOp(Op::Or);
        }
    }
    void parseAnd()
    {
        parseShift();
        while (m_error.isEmpty() && take("&")) {
            parseShift();
            emitOp(Op::And);
        }
    }
    void parseShift()
    {
        parseSum();
        while (m_error.isEmpty()) {
            if (take("<<")) {
                parseSum();
                emitOp(Op::Shl);
            } else if (take(">>")) {
                parseSum();
                emitOp(Op::Shr);
            } else {
                break;
            }
        }
    }
    void parseSum()
    {
        parseTerm();
        while (m_error.isEmpty()) {
            if (take("+")) {
                parseTerm();
                emitOp(Op::Add);
            } else if (take("-")) {
                parseTerm();
                emitOp(Op::Sub);
            } else {
                break;
            }
        }
    }
    void parseTerm()
    {
        parseUnary();
        while (m_error.isEmpty()) {
            if (take("*")) {
                parseUnary();
                emitOp(Op::Mul);
            } else if (take("/")) {
                parseUnary();
                emitOp(Op::Div);
            } else {
                break;
            }
        }
    }
    void parseUnary()
    {
        if (++m_depth > 64) {
            fail(QStringLiteral("formula nested too deeply"));
            return;
        }
        if (take("-")) {
            parseUnary();
            emitOp(Op::Neg);
        } else {
            parsePrimary();
        }
        --m_depth;
    }
    // A ... Z, AA ... ZZ at the cursor, as a data byte index; -1 if none
    int takeByte()
    {
        skipSpace();
        int n = 0;
        while (m_pos + n < m_text.size() && m_text.at(m_pos + n) >= QLatin1Char('A')
               && m_text.at(m_pos + n) <= QLatin1Char('Z'))
            ++n;
        if (n < 1 || n > 2)
            return -1;
        const int first = m_text.at(m_pos).unicode() - 'A';
        const int index = n == 1 ? first : (first + 1) * 26 + (m_text.at(m_pos + 1).unicode() - 'A');
        m_pos += n;
        m_bytes = qMax(m_bytes, index + 1);
        return index;
    }
    void parsePrimary()
    {
        skipSpace();
        if (m_pos >= m_text.size()) {
            fail(QStringLiteral("formula ends early"));
            return;
        }
        const QChar c = m_text.at(m_pos);
        if (c == QLatin1Char('(')) {
            ++m_pos;
            parseOr();
            if (m_error.isEmpty() && !take(")"))
                fail(QStringLiteral("missing ')'"));
            return;
        }
        if (c == QLatin1Char('{')) {
            ++m_pos;
            const int byte = takeByte();
            if (byte < 0 || !take(":")) {
                fail(QStringLiteral("a bit is written {A:0}"));
                return;
            }
            skipSpace();
            const int bit = m_pos < m_text.size() ? m_text.at(m_pos).digitValue() : -1;
            if (bit < 0 || bit > 7) {
                fail(QStringLiteral("a bit number is 0 to 7"));
                return;
            }
            ++m_pos;
            if (!take("}")) {
                fail(QStringLiteral("missing '}'"));
                return;
            }
            m_code.append({Op::Bit, 0, byte, bit});
            return;
        }
        if (c.isDigit() || c == QLatin1Char('.')) {
            int end = m_pos;
            bool ok = false;
            double value = 0;
            if (QStringView(m_text).mid(m_pos).startsWith(QLatin1String("0X"))) {
                end += 2;
                while (end < m_text.size() && (m_text.at(end).isDigit()
                                               || (m_text.at(end) >= QLatin1Char('A') && m_text.at(end) <= QLatin1Char('F'))))
                    ++end;
                value = double(m_text.mid(m_pos + 2, end - m_pos - 2).toULongLong(&ok, 16));
            } else {
                while (end < m_text.size() && (m_text.at(end).isDigit() || m_text.at(end) == QLatin1Char('.')))
                    ++end;
                value = m_text.mid(m_pos, end - m_pos).toDouble(&ok);
            }
            if (!ok) {
                fail(QStringLiteral("not a number"));
                return;
            }
            m_pos = end;
            m_code.append({Op::Number, value, 0, 0});
            return;
        }
        if (take("SIGNED")) {
            if (!take("(")) {
                fail(QStringLiteral("SIGNED needs '('"));
                return;
            }
            parseOr();
            if (m_error.isEmpty() && !take(")"))
                fail(QStringLiteral("missing ')'"));
            emitOp(Op::Signed);
            return;
        }
        const int byte = takeByte();
        if (byte < 0) {
            fail(QStringLiteral("unexpected '%1'").arg(c));
            return;
        }
        m_code.append({Op::Byte, 0, byte, 0});
    }

    QString m_text;
    int m_pos = 0;
    int m_depth = 0;
    int m_bytes = 0;
    QString m_error;
    QVector<PidFormula::Step> m_code;
};

PidFormula PidFormula::compile(const QString &text, QString *error)
{
    PidFormula f;
    FormulaParser(text).run(&f, error);
    return f;
}

std::optional<double> PidFormula::evaluate(const QVector<uint8_t> &bytes) const
{
    if (m_code.isEmpty() || bytes.size() < m_bytes)
        return std::nullopt;
    QVarLengthArray<double, 32> stack;
    auto pop = [&stack] {
        const double v = stack.last();
        stack.removeLast();
        return v;
    };
    // Bit operations work on whole numbers
    auto whole = [](double v) { return static_cast<qint64>(std::llround(v)); };
    for (const Step &s : m_code) {
        switch (s.op) {
        case Op::Number: stack.append(s.number); continue;
        case Op::Byte: stack.append(bytes.at(s.byte)); continue;
        case Op::Bit: stack.append((bytes.at(s.byte) >> s.bit) & 1); continue;
        case Op::Neg: stack.last() = -stack.last(); continue;
        case Op::Signed: {
            const qint64 v = whole(stack.last()) & 0xFF;
            stack.last() = double(v >= 0x80 ? v - 0x100 : v);
            continue;
        }
        default: break;
        }
        const double b = pop();
        double &a = stack.last();
        switch (s.op) {
        case Op::Add: a += b; break;
        case Op::Sub: a -= b; break;
        case Op::Mul: a *= b; break;
        case Op::Div:
            if (b == 0)
                return std::nullopt;
            a /= b;
            break;
        case Op::And: a = double(whole(a) & whole(b)); break;
        case Op::Or: a = double(whole(a) | whole(b)); break;
        case Op::Shl: a = double(whole(a) << std::clamp<qint64>(whole(b), 0, 62)); break;
        case Op::Shr: a = double(whole(a) >> std::clamp<qint64>(whole(b), 0, 62)); break;
        default: break;
        }
    }
    const double result = stack.last();
    return std::isfinite(result) ? std::optional<double>(result) : std::nullopt;
}

// ── The file ─────────────────────────────────────────────────────────

namespace {

// "22", "0x22" or the number 34
std::optional<int> hexField(const QJsonValue &v)
{
    if (v.isDouble())
        return v.toInt(-1) >= 0 ? std::optional<int>(v.toInt()) : std::nullopt;
    QString s = v.toString().trimmed();
    if (s.startsWith(QLatin1String("0x"), Qt::CaseInsensitive))
        s = s.mid(2);
    bool ok = false;
    const int n = s.toInt(&ok, 16);
    return ok && n >= 0 ? std::optional<int>(n) : std::nullopt;
}

const QStringList &kinds()
{
    static const QStringList k{QStringLiteral("numeric"), QStringLiteral("percentage"), QStringLiteral("temperature"),
                               QStringLiteral("voltage"), QStringLiteral("pressure"), QStringLiteral("bidirectional")};
    return k;
}

// One entry, or why not
std::optional<CustomPid> parseEntry(const QJsonObject &o, QString *why)
{
    static const QRegularExpression idRe(QStringLiteral("^[A-Z][A-Z0-9_]{1,39}$"));
    static const QRegularExpression headerRe(QStringLiteral("^([0-9A-F]{3}|[0-9A-F]{6})$"));

    CustomPid p;
    p.meta.id = o.value(QLatin1String("id")).toString();
    if (!idRe.match(p.meta.id).hasMatch()) {
        *why = QStringLiteral("\"id\" must be 2 to 40 capitals, digits and _ (TRANS_TEMP)");
        return std::nullopt;
    }
    if (ParameterCatalog::isBuiltinId(p.meta.id)) {
        *why = QStringLiteral("OCTAVE already has a parameter with this id");
        return std::nullopt;
    }
    p.meta.title = o.value(QLatin1String("title")).toString(p.meta.id).left(60);
    p.meta.shortTitle = o.value(QLatin1String("shortTitle")).toString().left(8);
    p.meta.unit = o.value(QLatin1String("unit")).toString().left(12);
    p.meta.min = o.value(QLatin1String("min")).toDouble(0);
    p.meta.max = o.value(QLatin1String("max")).toDouble(100);
    if (!(p.meta.max > p.meta.min)) {
        *why = QStringLiteral("\"max\" must be above \"min\"");
        return std::nullopt;
    }
    p.meta.kind = o.value(QLatin1String("kind")).toString(QStringLiteral("numeric"));
    if (!kinds().contains(p.meta.kind)) {
        *why = QStringLiteral("\"kind\" must be one of %1").arg(kinds().join(QLatin1String(", ")));
        return std::nullopt;
    }
    p.meta.decimals = qBound(0, o.value(QLatin1String("decimals")).toInt(1), 4);

    const std::optional<int> mode = o.contains(QLatin1String("mode")) ? hexField(o.value(QLatin1String("mode")))
                                                                      : std::optional<int>(0x22);
    if (!mode || !ELM327Protocol::isReadDataMode(*mode)) {
        *why = QStringLiteral("\"mode\" must be \"22\" or \"21\" (reading only)");
        return std::nullopt;
    }
    p.mode = *mode;
    const std::optional<int> pid = hexField(o.value(QLatin1String("pid")));
    const int maxPid = p.mode == 0x22 ? 0xFFFF : 0xFF;
    if (!pid || *pid > maxPid) {
        *why = p.mode == 0x22 ? QStringLiteral("\"pid\" must be up to four hex digits (\"1940\")")
                              : QStringLiteral("\"pid\" must be up to two hex digits for mode 21 (\"01\")");
        return std::nullopt;
    }
    p.pid = *pid;
    p.header = o.value(QLatin1String("header")).toString().remove(QLatin1Char(' ')).toUpper();
    if (!p.header.isEmpty() && !headerRe.match(p.header).hasMatch()) {
        *why = QStringLiteral("\"header\" must be three or six hex digits (\"7E1\", \"DA10F1\")");
        return std::nullopt;
    }
    p.formulaText = o.value(QLatin1String("formula")).toString();
    QString formulaError;
    p.formula = PidFormula::compile(p.formulaText, &formulaError);
    if (!p.formula.isValid()) {
        *why = QStringLiteral("\"formula\": %1").arg(formulaError);
        return std::nullopt;
    }
    return p;
}

} // namespace

CustomPids::Parsed CustomPids::parse(const QByteArray &json)
{
    Parsed out;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError) {
        out.errors.append(QStringLiteral("Not valid JSON: %1 (character %2)").arg(err.errorString()).arg(err.offset));
        return out;
    }
    const QJsonArray entries = doc.isArray() ? doc.array() : doc.object().value(QLatin1String("pids")).toArray();
    QSet<QString> seen;
    for (int i = 0; i < entries.size(); ++i) {
        const QJsonObject o = entries.at(i).toObject();
        const QString id = o.value(QLatin1String("id")).toString();
        const QString label = id.isEmpty() ? QStringLiteral("Entry %1").arg(i + 1)
                                           : QStringLiteral("Entry %1 (%2)").arg(i + 1).arg(id.left(40));
        if (out.pids.size() >= kMaxPids) {
            out.errors.append(QStringLiteral("%1 and those after it: only the first %2 are used").arg(label).arg(kMaxPids));
            break;
        }
        QString why;
        const std::optional<CustomPid> p = parseEntry(o, &why);
        if (p && seen.contains(p->meta.id))
            why = QStringLiteral("this id is used by an entry above");
        if (!p || !why.isEmpty()) {
            out.errors.append(QStringLiteral("%1: %2").arg(label, why));
            continue;
        }
        seen.insert(p->meta.id);
        out.pids.append(*p);
    }
    return out;
}

CustomPids::CustomPids(const QString &path, QObject *parent)
    : QObject(parent)
    , m_file(path.isEmpty() ? QDir(SettingsManager::getAppDataDir()).filePath(QStringLiteral("custom-pids.json")) : path)
{
    QFile f(m_file);
    if (!f.exists())
        return;
    if (!f.open(QIODevice::ReadOnly)) {
        m_errors.append(QStringLiteral("Can't read the file: %1").arg(f.errorString()));
    } else {
        Parsed parsed = parse(f.readAll());
        m_pids = parsed.pids;
        m_errors = parsed.errors;
    }
    QList<ParameterCatalog::Parameter> rows;
    for (const CustomPid &p : m_pids)
        rows.append(p.meta);
    if (!rows.isEmpty() && !ParameterCatalog::addCustom(rows)) {
        // Built after the catalog was read: a wiring mistake, not the user's
        m_errors.append(QStringLiteral("The custom PIDs were read too late to be used (an OCTAVE bug)."));
        m_pids.clear();
    }
    for (const QString &e : m_errors)
        qCWarning(lcCustomPids).noquote() << m_file << ":" << e;
    qCInfo(lcCustomPids) << m_pids.size() << "custom PIDs from" << m_file;
}

bool CustomPids::fileExists() const
{
    return QFile::exists(m_file);
}

QVariantList CustomPids::pids() const
{
    QVariantList list;
    for (const CustomPid &p : m_pids) {
        list.append(QVariantMap{
            {QStringLiteral("id"), p.meta.id},
            {QStringLiteral("title"), p.meta.title},
            {QStringLiteral("unit"), p.meta.unit},
            {QStringLiteral("mode"), QString::number(p.mode, 16).toUpper()},
            {QStringLiteral("pid"), QString::number(p.pid, 16).toUpper().rightJustified(p.mode == 0x22 ? 4 : 2, QLatin1Char('0'))},
            {QStringLiteral("header"), p.header},
            {QStringLiteral("formula"), p.formulaText},
        });
    }
    return list;
}

QList<CustomPidRequest> CustomPids::requests() const
{
    QList<CustomPidRequest> list;
    for (const CustomPid &p : m_pids) {
        const PidFormula formula = p.formula;
        list.append({p.meta.id, p.mode, p.pid, p.header, [formula](const QVector<uint8_t> &bytes) {
                         return formula.evaluate(bytes).value_or(std::numeric_limits<double>::quiet_NaN());
                     }});
    }
    return list;
}

bool CustomPids::createExample()
{
    if (fileExists())
        return false;
    // The first entry is a real one (GM automatic transmission fluid
    // temperature) that the simulated car also answers, so `octave --sim`
    // shows it working. "about" is ignored by the reader.
    static const char example[] = R"json({
  "schema": 1,
  "about": "Readings beyond standard OBD-II. See docs/CUSTOM_PIDS.md. Restart OCTAVE after editing.",
  "pids": [
    {
      "id": "TRANS_TEMP",
      "title": "Transmission Temp",
      "shortTitle": "TFT",
      "unit": "°C",
      "min": -40,
      "max": 150,
      "kind": "temperature",
      "decimals": 0,
      "mode": "22",
      "pid": "1940",
      "header": "7E1",
      "formula": "A-40"
    }
  ]
}
)json";
    QDir().mkpath(QFileInfo(m_file).absolutePath());
    QSaveFile f(m_file);
    if (!f.open(QIODevice::WriteOnly) || f.write(example) < 0 || !f.commit()) {
        qCWarning(lcCustomPids) << "could not write" << m_file << ":" << f.errorString();
        return false;
    }
    emit fileExistsChanged();
    return true;
}
