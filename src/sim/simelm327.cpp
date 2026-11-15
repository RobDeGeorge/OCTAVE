#include "simelm327.h"

#include "carsim.h"
#include "../managers/elm327protocol.h"

#include <QDateTime>
#include <QRandomGenerator>
#include <QSet>
#include <QThread>

#include <algorithm>
#include <cmath>

namespace OctaveSim {

namespace {

double uniform(double lo, double hi)
{
    return lo + (hi - lo) * QRandomGenerator::global()->generateDouble();
}

// Inverses of the decoders in elm327protocol.cpp
uint8_t u8(double v) { return static_cast<uint8_t>(std::clamp(std::lround(v), 0L, 255L)); }
uint8_t pct(double v) { return u8(v * 255.0 / 100.0); }            // A*100/255
uint8_t trim(double v) { return u8(v * 128.0 / 100.0 + 128.0); }    // (A-128)*100/128
uint8_t temp(double c) { return u8(c + 40.0); }                    // A-40
QByteArray raw(std::initializer_list<long> b)
{
    QByteArray out;
    for (long x : b)
        out.append(static_cast<char>(x & 0xFF));
    return out;
}
QByteArray u16(double v)
{
    const long r = std::clamp(std::lround(v), 0L, 65535L);
    return raw({r >> 8, r});
}
QByteArray s16(double v)
{
    const long r = std::clamp(std::lround(v), -32768L, 32767L) & 0xFFFF;
    return raw({r >> 8, r});
}
QByteArray u32(double v)
{
    const long r = static_cast<long>(static_cast<quint32>(std::max(0.0, std::round(v))));
    return raw({r >> 24, r >> 16, r >> 8, r});
}
QByteArray bytes(std::initializer_list<uint8_t> b)
{
    QByteArray out;
    for (uint8_t x : b)
        out.append(static_cast<char>(x));
    return out;
}
QByteArray o2(double volts) { return bytes({u8(volts * 200.0), 0xFF}); }   // B = trim n/a

// Every Mode 01 PID the protocol tables know, plus PID 01 (monitor status)
const QSet<int> &supportedPids()
{
    static const QSet<int> pids = [] {
        QSet<int> s{0x01};
        for (auto it = ELM327Protocol::pidTable().constBegin(); it != ELM327Protocol::pidTable().constEnd(); ++it)
            if (it.key().first == 1)
                s.insert(it.key().second);
        for (auto it = ELM327Protocol::extendedPidTable().constBegin();
             it != ELM327Protocol::extendedPidTable().constEnd(); ++it)
            if (it.key().first == 1)
                s.insert(it.key().second);
        return s;
    }();
    return pids;
}

// "P0420" -> two DTC bytes
QByteArray encodeDtc(const QString &code)
{
    if (code.size() != 5)
        return bytes({0, 0});
    static const QString letters = QStringLiteral("PCBU");
    const int type = std::max<int>(0, letters.indexOf(code.at(0).toUpper()));
    bool ok1 = false, ok2 = false;
    const int d1 = code.mid(1, 1).toInt(&ok1, 16);
    const int rest = code.mid(2).toInt(&ok2, 16);
    if (!ok1 || !ok2)
        return bytes({0, 0});
    return bytes({static_cast<uint8_t>((type << 6) | ((d1 & 0x3) << 4) | ((rest >> 8) & 0xF)),
                  static_cast<uint8_t>(rest & 0xFF)});
}

// NV3550 ratios (the Jeep the simulated VIN belongs to)
constexpr double kGearRatios[] = {4.02, 2.32, 1.40, 1.00, 0.78, 0.78};

} // namespace

SimElm327Device::SimElm327Device(std::shared_ptr<CarState> state, QObject *parent)
    : QIODevice(parent)
    , m_state(std::move(state))
{
}

bool SimElm327Device::open(OpenMode mode)
{
    if (m_state->linkDown()) {
        setErrorString(QStringLiteral("Host is down (simulated Bluetooth dropout)"));
        return false;
    }
    m_input.clear();
    m_ready.clear();
    m_pending.clear();
    m_spaces = true;
    m_echo = true;
    m_clock.start();
    return QIODevice::open(mode | QIODevice::Unbuffered);
}

bool SimElm327Device::dropLink()
{
    if (!m_state->linkDown())
        return false;
    setErrorString(QStringLiteral("Bluetooth link lost (simulated)"));
    close();
    return true;
}

qint64 SimElm327Device::bytesAvailable() const
{
    qint64 n = m_ready.size();
    const qint64 now = m_clock.isValid() ? m_clock.elapsed() : 0;
    for (const Pending &p : m_pending)
        if (p.readyAtMs <= now)
            n += p.bytes.size();
    return n + QIODevice::bytesAvailable();
}

bool SimElm327Device::waitForReadyRead(int msecs)
{
    if (!isOpen() || dropLink())
        return false;
    auto promote = [this] {
        const qint64 now = m_clock.elapsed();
        while (!m_pending.isEmpty() && m_pending.first().readyAtMs <= now)
            m_ready.append(m_pending.takeFirst().bytes);
    };
    pumpMonitor();
    promote();
    if (m_ready.isEmpty()) {
        if (m_pending.isEmpty()) {
            QThread::msleep(static_cast<unsigned long>(std::clamp(msecs, 0, 20)));
            return false;
        }
        const qint64 wait = m_pending.first().readyAtMs - m_clock.elapsed();
        if (msecs >= 0 && wait > msecs) {
            QThread::msleep(static_cast<unsigned long>(msecs));
            return false;
        }
        if (wait > 0)
            QThread::msleep(static_cast<unsigned long>(wait));
        promote();
    }
    emit readyRead();
    return !m_ready.isEmpty();
}

qint64 SimElm327Device::readData(char *data, qint64 maxSize)
{
    pumpMonitor();
    const qint64 now = m_clock.elapsed();
    while (!m_pending.isEmpty() && m_pending.first().readyAtMs <= now)
        m_ready.append(m_pending.takeFirst().bytes);
    const qint64 n = std::min<qint64>(maxSize, m_ready.size());
    std::copy_n(m_ready.constData(), n, data);
    m_ready.remove(0, static_cast<int>(n));
    return n;
}

void SimElm327Device::pumpMonitor()
{
    if (!m_monitoring || m_state->linkDown())
        return;
    struct Message {
        int id;
        int everyMs;
    };
    static constexpr Message kMessages[] = {{0x0C9, 20}, {0x1F5, 50}, {0x3E9, 50},
                                            {0x4C1, 100}, {0x4D1, 500}, {0x120, 1000}};
    auto passes = [this](const QByteArray &id) {
        if (m_receiveFilter.isEmpty())
            return true;
        if (m_receiveFilter.size() != id.size())
            return false;
        for (int i = 0; i < id.size(); ++i) {
            if (m_receiveFilter.at(i) != 'X' && m_receiveFilter.at(i) != id.at(i))
                return false;
        }
        return true;
    };
    const qint64 now = m_clock.elapsed();
    const CarValues car = m_state->values();
    for (qint64 t = m_monitorAtMs + 1; t <= now && m_monitoring; ++t) {
        for (const Message &m : kMessages) {
            if (t % m.everyMs)
                continue;
            const QByteArray id = QByteArray::number(m.id, 16).toUpper().rightJustified(3, '0');
            if (!passes(id))
                continue;
            QByteArray d(8, '\0');
            auto put16 = [&d](int at, int v) {
                d[at] = static_cast<char>((v >> 8) & 0xFF);
                d[at + 1] = static_cast<char>(v & 0xFF);
            };
            switch (m.id) {
            case 0x0C9:
                put16(1, qRound(car.rpm * 4.0));
                d[4] = static_cast<char>(qBound(0, qRound(car.throttle * 2.55), 255));
                d[7] = static_cast<char>((t / m.everyMs) & 0x0F);   // rolling counter
                break;
            case 0x1F5:
                d[0] = static_cast<char>(car.gear & 0x0F);
                d[3] = car.brake > 1.0 ? '\x10' : '\0';
                break;
            case 0x3E9:
                put16(0, qRound(car.speedMph * 1.609344 * 100.0));
                put16(2, qRound(car.speedMph * 1.609344 * 100.0));
                break;
            case 0x4C1:
                d[2] = static_cast<char>(qBound(0, qRound(car.coolant + 40.0), 255));
                d[3] = static_cast<char>(qBound(0, qRound(car.intakeTemp + 40.0), 255));
                break;
            case 0x4D1:
                d[5] = static_cast<char>(qBound(0, qRound(car.fuelLevel * 2.55), 255));
                break;
            default: {
                const quint32 km = static_cast<quint32>(car.odometerKm);
                put16(0, static_cast<int>(km >> 16));
                put16(2, static_cast<int>(km & 0xFFFF));
                break;
            }
            }
            const char sep = m_spaces ? ' ' : '\0';
            QByteArray line = d.toHex(sep).toUpper();
            if (m_headersOn)
                line.prepend(m_spaces ? id + ' ' : id);
            m_pending.append({line + "\r", t});
            if (m_receiveFilter.isEmpty() && ++m_monitorSent >= kMonitorBuffer) {
                m_pending.append({QByteArrayLiteral("BUFFER FULL\r\r>"), t});
                m_monitoring = false;
                break;
            }
        }
    }
    m_monitorAtMs = now;
}

qint64 SimElm327Device::writeData(const char *data, qint64 size)
{
    if (m_monitoring && size > 0) {
        // Any byte ends monitor mode, and is not taken as a command
        m_monitoring = false;
        m_input.clear();
        const qint64 base = m_pending.isEmpty() ? m_clock.elapsed() : m_pending.last().readyAtMs;
        m_pending.append({QByteArrayLiteral("STOPPED\r\r>"), base + 8});
        return size;
    }
    m_input.append(data, static_cast<int>(size));
    int cr;
    while ((cr = m_input.indexOf('\r')) >= 0) {
        const QByteArray command = m_input.left(cr);
        m_input.remove(0, cr + 1);
        if (m_state->linkDown())
            continue;   // the bytes went nowhere
        const LinkSettings faults = m_state->link();
        if (faults.silent)
            continue;   // something on the port that is not an ELM327
        const bool echoed = m_echo;   // as it was when the command arrived
        QByteArray reply = respond(command);
        if (m_monitoring)
            continue;   // "ATMA": messages follow, no prompt
        // Headers on: each reply line starts with the answering ECU's id
        // and the frame's length byte
        const QByteArray asked = command.trimmed().toUpper();
        QByteArray digits = reply;
        digits.replace(' ', "");
        const bool hex = !digits.isEmpty() && digits.size() % 2 == 0
                         && std::all_of(digits.cbegin(), digits.cend(), [](char c) {
                                return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
                            });
        if (m_headersOn && !asked.startsWith("AT") && hex) {
            const QByteArray length = "0" + QByteArray::number(digits.size() / 2, 16).toUpper();
            reply.prepend(m_spaces ? "7E8 " + length + " " : "7E8" + length);
        }
        if (echoed)
            reply.prepend(command + "\r");
        const qint64 base = m_pending.isEmpty() ? m_clock.elapsed() : m_pending.last().readyAtMs;
        const QByteArray bytes = reply + "\r\r>";
        const qint64 at = base + latencyMs(command);
        if (faults.chunkBytes > 0) {
            // A slow serial link: the reply trickles in, 2 ms a piece
            int piece = 0;
            for (int pos = 0; pos < bytes.size(); pos += faults.chunkBytes, ++piece)
                m_pending.append({bytes.mid(pos, faults.chunkBytes), at + piece * 2});
        } else {
            m_pending.append({bytes, at});
        }
    }
    return size;
}

int SimElm327Device::latencyMs(const QByteArray &command) const
{
    const QByteArray cmd = command.trimmed().toUpper();
    const bool realistic = m_state->link().realistic;
    if (cmd.startsWith("ATZ"))
        return realistic ? 700 : 50;
    if (!realistic)
        return 8;   // never 0: the worker polls as fast as replies come
    if (cmd.startsWith("AT"))
        return 15;
    if (cmd == "03" || cmd == "07" || cmd == "0902" || cmd.startsWith("0202"))
        return 120;
    return static_cast<int>(uniform(35, 70));   // a Bluetooth SPP round trip
}

QByteArray SimElm327Device::formatMessage(const QByteArray &payload) const
{
    const char sep = m_spaces ? ' ' : '\0';   // toHex() leaves '\0' out
    auto hex = [sep](const QByteArray &b) { return b.toHex(sep).toUpper(); };
    if (payload.size() <= 7)
        return hex(payload);
    // CAN multi-frame (ISO 15765-4, headers off): byte count, then frames of
    // 6 and 7 bytes numbered 0-F
    QByteArray out = QByteArray::number(payload.size(), 16).toUpper().rightJustified(3, '0');
    int index = 0;
    for (int pos = 0; pos < payload.size(); ++index) {
        const int len = index == 0 ? 6 : 7;
        out += "\r" + QByteArray::number(index % 16, 16).toUpper() + ":" + (m_spaces ? " " : "")
               + hex(payload.mid(pos, len));
        pos += len;
    }
    return out;
}

QByteArray SimElm327Device::respond(const QByteArray &command)
{
    QByteArray cmd = command.trimmed().toUpper();
    cmd.replace(' ', "");
    if (cmd.isEmpty())
        return QByteArray();

    if (cmd.startsWith("AT")) {
        if (cmd == "ATZ" || cmd == "ATWS") {
            m_spaces = true;
            m_echo = true;
            m_probed = false;
            m_header.clear();
            m_headersOn = false;
            m_receiveFilter.clear();
            return "ELM327 v1.5";
        }
        if (cmd == "ATE0" || cmd == "ATE1") {
            m_echo = cmd == "ATE1";
            return "OK";
        }
        if (cmd == "ATH0" || cmd == "ATH1") {
            m_headersOn = cmd == "ATH1";
            return "OK";
        }
        if (cmd.startsWith("ATCRA")) {
            m_receiveFilter = cmd.mid(5);
            return "OK";
        }
        if (cmd == "ATAR") {
            m_receiveFilter.clear();
            return "OK";
        }
        if (cmd == "ATMA") {
            m_monitoring = true;
            m_monitorAtMs = m_clock.elapsed();
            m_monitorSent = 0;
            return QByteArray();
        }
        if (cmd == "ATI")
            return "ELM327 v1.5";
        if (cmd == "ATRV")
            return QByteArray::number(m_state->values().voltage, 'f', 1) + "V";
        if (cmd == "ATS0" || cmd == "ATS1") {
            m_spaces = cmd == "ATS1";
            return "OK";
        }
        if (cmd == "ATDP")
            return "AUTO, ISO 15765-4 (CAN 11/500)";
        if (cmd == "ATDPN")
            return "A6";
        if (cmd.startsWith("ATSH")) {
            // The request address: 7DF (everyone) and 7E0 reach the engine
            // ECU, 7E1 the transmission's; nobody is at any other
            m_header = cmd.mid(4);
            if (m_header == "7DF")
                m_header.clear();
            return "OK";
        }
        // The other commands an ELM327 knows and this one accepts without
        // modelling; anything else is "?", as on the real thing, so a typo
        // in the worker's setup or a new command shows up in the tests
        static const char *const kAccepted[] = {"ATL0", "ATL1", "ATM0", "ATM1", "ATD", "ATPC", "ATAL", "ATNL",
                                                "ATAT0", "ATAT1", "ATAT2", "ATCAF0", "ATCAF1",
                                                "ATCSM0", "ATCSM1", "ATCFC0", "ATCFC1"};
        for (const char *known : kAccepted)
            if (cmd == known)
                return "OK";
        if (cmd.startsWith("ATSP") || cmd.startsWith("ATTP") || cmd.startsWith("ATST")
            || cmd.startsWith("ATCF") || cmd.startsWith("ATCM") || cmd.startsWith("ATFC"))
            return "OK";
        return "?";
    }

    // Ignition off: the adapter searches every protocol and finds no ECU
    if (m_state->link().ignitionOff)
        return "SEARCHING...\rUNABLE TO CONNECT";

    bool ok = false;
    const int mode = cmd.left(2).toInt(&ok, 16);
    if (!ok)
        return "?";
    const int pid = cmd.size() >= 4 ? cmd.mid(2, 2).toInt(&ok, 16) : -1;
    if (!ok)
        return "?";

    // Manufacturer-specific readings (mode 22), for custom PIDs. The
    // transmission ECU answers 1940, fluid temperature as GM has it (one
    // byte, C + 40), only when addressed (ATSH7E1); the engine ECU answers
    // 2000, battery millivolts in two bytes, a made-up id.
    if (mode == 0x22 && cmd.size() == 6) {
        const int did = cmd.mid(2, 4).toInt(&ok, 16);
        const CarValues car = m_state->values();
        QByteArray data;
        if (ok && did == 0x1940 && m_header == "7E1")
            data.append(static_cast<char>(qBound(0, qRound(car.oil - 8.0 + 40.0), 255)));
        else if (ok && did == 0x2000 && (m_header.isEmpty() || m_header == "7E0")) {
            const int mv = qBound(0, qRound(car.voltage * 1000.0), 65535);
            data.append(static_cast<char>(mv >> 8)).append(static_cast<char>(mv & 0xFF));
        }
        if (data.isEmpty()) {
            // The ECU spoken to refuses an id it does not have (7F 22 31,
            // request out of range); an address nobody is at stays silent
            if (m_header == "7E1" || m_header == "7E0")
                return formatMessage(QByteArray::fromHex("7F2231"));
            return "NO DATA";
        }
        return formatMessage(QByteArray::fromHex("62" + cmd.mid(2, 4)) + data);
    }
    // Everything below is the engine ECU's
    if (!m_header.isEmpty() && m_header != "7E0")
        return "NO DATA";

    auto reply = [this](int respMode, int respPid, const QByteArray &data) {
        QByteArray payload;
        payload.append(static_cast<char>(respMode));
        if (respPid >= 0)
            payload.append(static_cast<char>(respPid));
        return formatMessage(payload + data);
    };

    // Several PIDs in one request ("010C0D05", CAN): one reply with each
    // supported PID and its data, NO DATA if none is. An odd length ends in
    // a response count ("010C1"), which a single request may carry.
    const QByteArray pidHex = cmd.mid(2, (cmd.size() - 2) & ~1);
    if (mode == 0x01 && pidHex.size() >= 4 && pidHex.size() <= 12) {
        QByteArray payload(1, '\x41');
        for (int i = 0; i < pidHex.size(); i += 2) {
            const int p = pidHex.mid(i, 2).toInt(&ok, 16);
            if (!ok)
                return "?";
            const QByteArray data = mode01(p);
            if (!data.isEmpty())
                payload += char(p) + data;
        }
        if (payload.size() == 1)
            return "NO DATA";
        const LinkSettings link = m_state->link();
        if (m_probed && link.realistic && QRandomGenerator::global()->generateDouble() < link.failureRate)
            return "NO DATA";
        m_state->notePoll();
        return formatMessage(payload);
    }

    if (mode == 0x01 && pid >= 0) {
        const QByteArray data = mode01(pid);
        if (data.isEmpty())
            return "NO DATA";
        // Bluetooth byte loss, ~2 % of polls. Never on the
        // connect probe or the scan queries: a lost bitmap would leave the
        // vehicle gate with a random partial set, a lost probe a 10 s retry
        const bool scanQuery = pid == 0x01 || pid % 0x20 == 0;
        const LinkSettings link = m_state->link();
        if (m_probed && !scanQuery && link.realistic
            && QRandomGenerator::global()->generateDouble() < link.failureRate)
            return "NO DATA";
        m_probed = true;
        m_state->notePoll();
        return reply(0x41, pid, data);
    }

    const CarValues v = m_state->values();
    auto dtcList = [&reply](int respMode, const QStringList &codes) {
        QByteArray data;
        data.append(static_cast<char>(codes.size()));   // CAN: count byte first
        for (const QString &c : codes)
            data += encodeDtc(c);
        return reply(respMode, -1, data);
    };
    switch (mode) {
    case 0x03:
        return dtcList(0x43, v.storedDtcs);
    case 0x07:
        return dtcList(0x47, v.pendingDtcs);
    case 0x0A:
        return dtcList(0x4A, {});
    case 0x04:
        // Refused, as many ECUs do with the engine running: 7F 04 22
        if (m_state->link().refuseClear)
            return formatMessage(QByteArray::fromHex("7F0422"));
        m_state->updateValues([](CarValues &c) {
            c.storedDtcs.clear();
            c.pendingDtcs.clear();
            c.freezeFrameDtc.clear();
        });
        return "44";
    case 0x02: {
        // Freeze frame 0: the code that stored it, and its PIDs. The
        // simulator has no snapshot: a frame's values are the car's now.
        if (v.freezeFrameDtc.isEmpty())
            return "NO DATA";
        if (pid == 0x02)
            return reply(0x42, 0x02, QByteArray(1, '\0') + encodeDtc(v.freezeFrameDtc));
        if (pid > 0x20)   // one bitmap range, as most ECUs keep
            return "NO DATA";
        const QByteArray data = mode01(pid == 0 ? 0 : pid);
        if (data.isEmpty())
            return "NO DATA";
        return reply(0x42, pid, QByteArray(1, '\0') + data);
    }
    case 0x09:
        if (pid == 0x02)
            return reply(0x49, 0x02, QByteArray(1, '\x01') + v.vin.toLatin1());
        return "NO DATA";
    default:
        return "NO DATA";
    }
}

QByteArray SimElm327Device::mode01(int pid)
{
    // Supported-PID bitmaps: 0x00, 0x20, ... 0xE0. Bit 32 of each says the
    // next range exists; a range nobody announced answers NO DATA.
    if (pid % 0x20 == 0) {
        auto anyAbove = [](int p) {
            return std::any_of(supportedPids().cbegin(), supportedPids().cend(), [p](int s) { return s > p; });
        };
        if (pid > 0 && !anyAbove(pid))
            return QByteArray();
        quint32 bitmap = 0;
        for (int i = 1; i <= 32; ++i) {
            if (pid + i == 0x0C && m_state->link().noRpm)
                continue;
            if (supportedPids().contains(pid + i) || (i == 32 && anyAbove(pid + 0x20)))
                bitmap |= 1u << (32 - i);
        }
        return u32(bitmap);
    }
    if (!supportedPids().contains(pid) || (pid == 0x0C && m_state->link().noRpm))
        return QByteArray();

    const CarValues v = m_state->values();
    const double t = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    const double cat = 400 + v.engineLoad * 2.5 + uniform(-3, 3);
    const double lambda = v.afr / 14.7;
    const double fuelGs = v.maf / std::max(1.0, v.afr);
    const double torqueActual = -5 + 90 * v.engineLoad / 100.0;

    switch (pid) {
    case 0x01: {   // MIL + DTC count, then monitor bytes
        const int n = static_cast<int>(v.storedDtcs.size());
        return bytes({static_cast<uint8_t>((n ? 0x80 : 0) | (n & 0x7F)), 0x07, 0xE5, 0x00});
    }
    case 0x04: return bytes({pct(v.engineLoad)});
    case 0x05: return bytes({temp(v.coolant)});
    case 0x06: return bytes({trim(v.stft)});
    case 0x07: return bytes({trim(v.ltft)});
    case 0x08: return bytes({trim(v.stft * 0.8)});
    case 0x09: return bytes({trim(v.ltft * 1.2)});
    case 0x0A: return bytes({u8(v.fuelPressure / 3.0)});
    case 0x0B: return bytes({u8(v.map)});
    case 0x0C: return u16(v.rpm * 4.0);
    case 0x0D: return bytes({u8(v.speedMph / 0.621371)});
    case 0x0E: return bytes({u8((v.timing + 64.0) * 2.0)});
    case 0x0F: return bytes({temp(v.intakeTemp)});
    case 0x10: return u16(v.maf * 100.0);
    case 0x11: return bytes({pct(v.throttle)});
    case 0x14: return o2(v.o2);
    case 0x15: return o2(0.70 + 0.02 * std::sin(t * 0.3));   // downstream sensors hold steady
    case 0x16: return o2(0.68);
    case 0x17: return o2(0.66);
    case 0x18: return o2(0.45 + std::sin(v.rpm / 400 + 1.0) * 0.3);
    case 0x19: return o2(0.69 + 0.02 * std::sin(t * 0.25));
    case 0x1A: return o2(0.67);
    case 0x1B: return o2(0.65);
    case 0x1F: return u16(v.runTimeS);
    case 0x21: return u16(v.storedDtcs.isEmpty() ? 0 : 37);
    case 0x22: return u16(v.fuelPressure / 0.079);
    case 0x23: return u16(300);
    case 0x2C: return bytes({pct(5 + v.engineLoad * 0.1)});
    case 0x2D: return bytes({trim(uniform(-1.5, 1.5))});
    case 0x2E: return bytes({pct(20)});
    case 0x2F: return bytes({pct(v.fuelLevel)});
    case 0x30: return bytes({37});
    case 0x31: return u16(v.storedDtcs.isEmpty() ? 0 : 1234);
    case 0x32: return s16(16 * 4.0);
    case 0x33: return bytes({u8(101 + uniform(-0.5, 0.5))});
    case 0x3C: return u16((cat + 40) * 10);
    case 0x3D: return u16((cat - 8 + 40) * 10);
    case 0x3E: return u16((cat - 50 + 40) * 10);
    case 0x3F: return u16((cat - 58 + 40) * 10);
    case 0x42: return u16(v.voltage * 1000.0);
    case 0x43: return u16(v.engineLoad * 255.0 / 100.0);
    case 0x44: return u16(lambda * 32768.0);
    case 0x45: return bytes({pct(v.throttle * 0.92)});
    case 0x46: return bytes({temp(22 + uniform(-1, 1))});
    case 0x47:
    case 0x48: return bytes({pct(v.throttle * 0.85 + 15)});
    case 0x49: return bytes({pct(v.throttle * 0.82 + 14)});
    case 0x4A:
    case 0x4B: return bytes({pct(v.throttle * 0.41 + 7)});
    case 0x4C: return bytes({pct(v.throttle)});
    case 0x4D: return u16(v.storedDtcs.isEmpty() ? 0 : 42);
    case 0x4E: return u16(4321);
    case 0x50: return bytes({30});
    case 0x51: return bytes({1});   // gasoline
    case 0x52: return bytes({pct(10)});
    case 0x53: return u16(100 * 200);
    case 0x54: return u16(64 + 32767);
    case 0x55:
    case 0x56:
    case 0x57:
    case 0x58: return bytes({trim(uniform(-1, 1))});
    case 0x59: return u16(v.fuelPressure / 10.0);
    case 0x5A: return bytes({pct(v.throttle)});
    case 0x5B: return bytes({pct(80)});
    case 0x5C: return bytes({temp(v.oil)});
    case 0x5D: return u16((-5 + 210) * 128.0);
    case 0x5E: return u16(fuelGs * 3600.0 / 745.0 * 20.0);   // L/h, petrol at 745 g/L

    // Multi-value PIDs (extendedPidTable): byte A carries the support bits
    case 0x61: return bytes({u8(v.throttle * 0.9 + 125)});
    case 0x62: return bytes({u8(torqueActual + 125)});
    case 0x63: return u16(320);
    case 0x65: return bytes({0x10, static_cast<uint8_t>(std::clamp(v.gear + (v.rpm > 2500 ? 1 : 0), 1, 6) << 4)});
    case 0x66: return bytes({0x01}) + u16(v.maf * 32.0);
    case 0x67: return bytes({0x03, temp(v.coolant), temp(v.coolant - 1)});
    case 0x68: return bytes({0x0B, temp(v.intakeTemp), temp(v.intakeTemp + 1), 0x00, temp(v.intakeTemp)});
    case 0x69: return bytes({0x07, pct(5 + v.engineLoad * 0.1), pct(5 + v.engineLoad * 0.1 - 0.5), u8(100 * 128.0 / 100.0)});
    case 0x6C: return bytes({0x03, pct(v.throttle), pct(v.throttle * 0.92)});
    case 0x6D: return bytes({0x07}) + u16(v.fuelPressure / 10.0) + u16(v.fuelPressure / 10.0 - 1) + bytes({temp(45)});
    case 0x70: return bytes({0x03}) + u16((100 + 125 * v.engineLoad / 100.0) * 32.0)
                      + u16((100 + 120 * v.engineLoad / 100.0) * 32.0);
    case 0x72: return bytes({0x03, pct(30), pct(28)});
    case 0x7F: return bytes({0x03}) + u32(12000000 + v.runTimeS) + u32(3100000);
    case 0x84: return bytes({temp(80)});
    case 0x8D: return bytes({pct(v.throttle)});
    case 0x8E: return bytes({u8(12 + 125)});
    case 0x9A: return bytes({0x06, 0x00}) + u16(201.6 * 64.0) + s16(-12.3 * 10.0);
    case 0x9D: return u16(fuelGs * 50.0) + u16(fuelGs * 50.0);
    case 0x9E: return u16(v.maf * 3.6 * 1.07 * 5.0);
    case 0xA2: return u16(20 * 32.0);
    case 0xA4: return bytes({0x02, 0x00}) + u16(kGearRatios[std::clamp(v.gear, 1, 6) - 1] * 1000.0);
    case 0xA6: return u32(v.odometerKm * 10.0);
    case 0xB2: return bytes({pct(92)});
    default:
        break;
    }

    // Wide-range O2 (0x24-0x2B voltage, 0x34-0x3B current): A,B lambda, C,D the reading
    if (pid >= 0x24 && pid <= 0x2B)
        return u16(lambda * 32768.0) + u16(v.o2 / 8.0 * 65535.0);
    if (pid >= 0x34 && pid <= 0x3B)
        return u16(lambda * 32768.0) + u16(((lambda - 1.0) * 2.0 + 128.0) * 256.0);
    return QByteArray();
}

} // namespace OctaveSim
