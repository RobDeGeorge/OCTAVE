#include "weblibrary.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QLoggingCategory>
#include <QMutex>
#include <QThread>
#include <QWaitCondition>

#include <taglib/tiostream.h>

#include <emscripten.h>
#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <algorithm>
#include <cstring>
#include <string>

Q_LOGGING_CATEGORY(lcWebMusic, "octave.media.web")

namespace {

const QString kBase = QStringLiteral("/weblibrary");
const QByteArray kMagic = QByteArrayLiteral("octave-weblibrary");
constexpr int kDecodeTimeoutMs = 60000;

// One part of a visitor's file, read at pick time
struct Extent {
    qint64 offset = 0;
    QByteArray bytes;
};
struct TagParts {
    qint64 size = 0;
    QList<Extent> extents;
};

// A decode the visualizer's worker waits for
struct Decode {
    bool done = false;
    std::vector<float> samples;
    int sampleRate = 0;
};

// Shared with worker threads (tagStream, decodeMono)
struct Shared {
    QMutex mutex;
    QWaitCondition decoded;
    int generation = 0;   // one per pick; placeholders of an older pick are gone
    QHash<int, TagParts> tags;   // by File index, this generation
    int nextDecode = 0;
    QHash<int, Decode> decodes;
};

Shared &shared()
{
    static Shared s;
    return s;
}

// Main thread only
QStringList g_extensions;
QString g_root;
int g_songs = 0;
std::function<void(const QString &, int)> g_imported;

// clang-format off
// The JS half: the hidden folder input, the File objects of the last pick,
// the tag parts read from them, blob: URLs and Web Audio decodes
EM_JS(void, octave_webmusic_install, (), {
    if (Module.octaveWebMusic)
        return;
    const PART_CAP = 16 << 20;       // the most read of one part (a moov atom, an ID3v2 tag)
    const TOTAL_CAP = 512 << 20;     // the most read for one library
    const AROUND = 65536;            // audio kept after the tags, and the end of each file

    const lib = {
        files: [],
        input: null,
        lastUrl: null,
        gen: 0,

        pick: function () {
            if (!lib.input) {
                const input = document.createElement("input");
                input.type = "file";
                input.webkitdirectory = true;
                input.multiple = true;
                input.style.display = "none";
                input.addEventListener("change", function () {
                    const files = Array.from(input.files || []);
                    input.value = "";   // so picking the same folder again is a change too
                    lib.files = files;
                    const r = Module.octaveWebMusicPicked(files.map(function (f) { return f.webkitRelativePath || f.name; }));
                    lib.readAll(r.gen, r.audio);
                });
                document.body.appendChild(input);
                lib.input = input;
            }
            lib.input.click();
        },

        // The parts of file f that TagLib needs, as [offset, Uint8Array]
        tagParts: async function (f) {
            const size = f.size;
            const read = async function (off, len) {
                off = Math.max(0, Math.min(off, size));
                return new Uint8Array(await f.slice(off, Math.min(size, off + Math.min(len, PART_CAP))).arrayBuffer());
            };
            const ext = (f.name.split(".").pop() || "").toLowerCase();
            const parts = [];
            let head = 0;          // where the tags at the start of the file end
            let after = AROUND;
            const h = await read(0, 10);
            if (h.length === 10 && h[0] === 0x49 && h[1] === 0x44 && h[2] === 0x33)   // "ID3"
                head = 10 + (((h[6] & 0x7f) << 21) | ((h[7] & 0x7f) << 14) | ((h[8] & 0x7f) << 7) | (h[9] & 0x7f))
                       + ((h[5] & 0x10) ? 10 : 0);
            if (ext === "flac") {
                // Metadata blocks after "fLaC", the last one flagged
                let p = head + 4;
                for (let n = 0; n < 256 && p + 4 <= size; ++n) {
                    const b = await read(p, 4);
                    if (b.length < 4)
                        break;
                    p += 4 + ((b[1] << 16) | (b[2] << 8) | b[3]);
                    if (b[0] & 0x80)
                        break;
                }
                head = p;
            } else if (ext === "m4a" || ext === "mp4") {
                // Top-level atoms: the tags are in moov, at the start or the end
                let p = 0;
                for (let n = 0; n < 64 && p + 8 <= size; ++n) {
                    const a = await read(p, 16);
                    const dv = new DataView(a.buffer);
                    let len = dv.getUint32(0);
                    const type = String.fromCharCode(a[4], a[5], a[6], a[7]);
                    if (len === 1 && a.length >= 16)
                        len = Number(dv.getBigUint64(8));
                    else if (len === 0)
                        len = size - p;
                    if (len < 8)
                        break;
                    if (type === "moov") {
                        parts.push([p, await read(p, len + 4096)]);
                        break;
                    }
                    p += len;
                }
            } else if (ext === "ogg" || ext === "opus") {
                after = 1 << 20;   // comments and a cover are in the first pages
            }
            parts.push([0, await read(0, head + after)]);
            if (size > AROUND)
                parts.push([size - AROUND, await read(size - AROUND, AROUND)]);
            return parts;
        },

        readAll: async function (gen, audio) {
            lib.gen = gen;
            let total = 0;
            let next = 0;
            const worker = async function () {
                while (next < audio.length && lib.gen === gen && total < TOTAL_CAP) {
                    const i = audio[next++];
                    try {
                        const parts = await lib.tagParts(lib.files[i]);
                        if (lib.gen !== gen)
                            return;
                        const offsets = [];
                        let len = 0;
                        parts.forEach(function (p) { offsets.push(p[0], p[1].length); len += p[1].length; });
                        const bytes = new Uint8Array(len);
                        let at = 0;
                        parts.forEach(function (p) { bytes.set(p[1], at); at += p[1].length; });
                        total += len;
                        Module.octaveWebMusicTags(gen, i, lib.files[i].size, offsets, bytes);
                    } catch (e) {
                        console.warn("OCTAVE: could not read the tags of " + lib.files[i].name + ": " + e);
                    }
                }
            };
            await Promise.all([worker(), worker(), worker(), worker(), worker(), worker(), worker(), worker()]);
            if (lib.gen === gen)
                Module.octaveWebMusicReady(gen);
        },

        // A fresh blob: URL for file i (Qt's player revokes the one it was
        // given once it has loaded); the previous one is let go here
        url: function (i) {
            const f = lib.files[i];
            if (!f)
                return "";
            if (lib.lastUrl)
                URL.revokeObjectURL(lib.lastUrl);
            lib.lastUrl = URL.createObjectURL(f);
            return lib.lastUrl;
        },

        // File i decoded by the browser, resampled to 8 kHz (an offline
        // context decodes at its own rate) and mixed to mono
        decode: function (id, i) {
            const f = lib.files[i];
            const fail = function (e) {
                console.warn("OCTAVE: the browser could not decode " + (f ? f.name : i) + ": " + e);
                Module.octaveWebMusicDecoded(id, 0, new Float32Array(0));
            };
            if (!f)
                return fail("no such file");
            f.arrayBuffer()
                .then(function (buf) { return new OfflineAudioContext(1, 1, 8000).decodeAudioData(buf); })
                .then(function (audio) {
                    const n = audio.length;
                    const mono = new Float32Array(n);
                    for (let c = 0; c < audio.numberOfChannels; ++c) {
                        const ch = audio.getChannelData(c);
                        for (let k = 0; k < n; ++k)
                            mono[k] += ch[k];
                    }
                    const scale = 1 / Math.max(1, audio.numberOfChannels);
                    for (let k = 0; k < n; ++k)
                        mono[k] *= scale;
                    Module.octaveWebMusicDecoded(id, audio.sampleRate, mono);
                })
                .catch(fail);
        },
    };
    Module.octaveWebMusic = lib;
});
// clang-format on

// clang-format off
EM_JS(double, octave_webmusic_position, (), {
    const all = document.getElementsByTagName("audio");
    return all.length ? all[all.length - 1].currentTime * 1000 : -1;
});
EM_JS(int, octave_webmusic_playing, (), {
    const all = document.getElementsByTagName("audio");
    return all.length ? (all[all.length - 1].paused ? 0 : 1) : -1;
});
// clang-format on

emscripten::val helper()
{
    return emscripten::val::module_property("octaveWebMusic");
}

bool isAudio(const QString &path)
{
    const QString lower = path.toLower();
    for (const QString &ext : std::as_const(g_extensions)) {
        if (lower.endsWith(ext))
            return true;
    }
    return false;
}

// The generation and File index a placeholder holds; false for any other file
bool readPlaceholder(const QString &filePath, int *generation, int *index)
{
    if (!filePath.startsWith(kBase + QLatin1Char('/')))
        return false;
    QFile stub(filePath);
    if (!stub.open(QIODevice::ReadOnly))
        return false;
    const QList<QByteArray> parts = stub.read(64).split(' ');
    if (parts.size() != 3 || parts[0] != kMagic)
        return false;
    bool okGen = false, okIndex = false;
    *generation = parts[1].toInt(&okGen);
    *index = parts[2].toInt(&okIndex);
    return okGen && okIndex;
}

// The browser's relative paths ("Music/Album/01 Song.mp3"), in File order.
// Makes the placeholders; returns {gen, audio: [File indices of songs]}.
emscripten::val onPicked(emscripten::val jsPaths)
{
    const std::vector<std::string> paths = emscripten::vecFromJSArray<std::string>(jsPaths);

    int generation;
    {
        Shared &s = shared();
        QMutexLocker lock(&s.mutex);
        generation = ++s.generation;
        s.tags.clear();
    }
    QDir(kBase).removeRecursively();
    const QString base = kBase + QLatin1Char('/') + QString::number(generation);

    QString root;
    std::vector<int> audio;
    for (size_t i = 0; i < paths.size(); ++i) {
        const QString rel = QDir::cleanPath(QString::fromStdString(paths[i]));
        if (rel.isEmpty() || rel.startsWith(QLatin1String("..")) || QDir::isAbsolutePath(rel))
            continue;
        if (root.isEmpty())
            root = base + QLatin1Char('/') + rel.section(QLatin1Char('/'), 0, 0);
        if (!isAudio(rel))
            continue;
        const QString path = base + QLatin1Char('/') + rel;
        QDir().mkpath(path.section(QLatin1Char('/'), 0, -2));
        QFile stub(path);
        if (!stub.open(QIODevice::WriteOnly)) {
            qCWarning(lcWebMusic) << "Could not write placeholder" << path << stub.errorString();
            continue;
        }
        stub.write(kMagic + ' ' + QByteArray::number(generation) + ' ' + QByteArray::number(qulonglong(i)));
        audio.push_back(int(i));
    }
    if (!root.isEmpty())
        QDir().mkpath(root);
    g_root = root;
    g_songs = int(audio.size());
    qCInfo(lcWebMusic) << "Picked" << paths.size() << "files," << g_songs << "songs, library at" << root
                       << "- reading their tags";

    emscripten::val result = emscripten::val::object();
    result.set("gen", generation);
    result.set("audio", emscripten::val::array(audio));
    return result;
}

// The tag parts of File `index`: (offset, length) pairs into `bytes`
void onTags(int generation, int index, double size, emscripten::val jsOffsets, emscripten::val bytes)
{
    const std::vector<double> offsets = emscripten::vecFromJSArray<double>(jsOffsets);
    const qsizetype total = qsizetype(bytes["length"].as<double>());
    QByteArray all(total, Qt::Uninitialized);
    emscripten::val(emscripten::typed_memory_view(size_t(total), reinterpret_cast<uint8_t *>(all.data())))
        .call<void>("set", bytes);

    TagParts parts;
    parts.size = qint64(size);
    qsizetype at = 0;
    for (size_t k = 0; k + 1 < offsets.size(); k += 2) {
        const qsizetype len = qsizetype(offsets[k + 1]);
        parts.extents.append({qint64(offsets[k]), all.mid(at, len)});
        at += len;
    }
    Shared &s = shared();
    QMutexLocker lock(&s.mutex);
    if (generation == s.generation)
        s.tags.insert(index, parts);
}

void onReady(int generation)
{
    {
        Shared &s = shared();
        QMutexLocker lock(&s.mutex);
        if (generation != s.generation)
            return;
        qCInfo(lcWebMusic) << "Read the tags of" << s.tags.size() << "of" << g_songs << "songs";
    }
    if (g_imported)
        g_imported(g_root, g_songs);
}

void onDecoded(int id, int sampleRate, emscripten::val mono)
{
    std::vector<float> samples = emscripten::convertJSArrayToNumberVector<float>(mono);
    Shared &s = shared();
    QMutexLocker lock(&s.mutex);
    auto it = s.decodes.find(id);
    if (it == s.decodes.end())
        return;   // the worker gave up waiting
    it->samples = std::move(samples);
    it->sampleRate = sampleRate;
    it->done = true;
    s.decoded.wakeAll();
}

// A file TagLib reads through: the extents read at pick time, zeros between
class PartsStream : public TagLib::IOStream
{
public:
    PartsStream(const QString &name, TagParts parts)
        : m_name(QFile::encodeName(name)), m_parts(std::move(parts)) {}

    TagLib::FileName name() const override { return m_name.constData(); }

    TagLib::ByteVector readBlock(size_t length) override
    {
        const qint64 n = std::min<qint64>(qint64(length), std::max<qint64>(0, m_parts.size - m_pos));
        if (n <= 0)
            return {};
        TagLib::ByteVector out(static_cast<unsigned int>(n), 0);
        for (const Extent &e : std::as_const(m_parts.extents)) {
            const qint64 from = std::max(m_pos, e.offset);
            const qint64 to = std::min(m_pos + n, e.offset + qint64(e.bytes.size()));
            if (from < to)
                std::memcpy(out.data() + (from - m_pos), e.bytes.constData() + (from - e.offset), size_t(to - from));
        }
        m_pos += n;
        return out;
    }

    // Read-only
    void writeBlock(const TagLib::ByteVector &) override {}
    void insert(const TagLib::ByteVector &, TagLib::offset_t, size_t) override {}
    void removeBlock(TagLib::offset_t, size_t) override {}
    void truncate(TagLib::offset_t) override {}
    bool readOnly() const override { return true; }
    bool isOpen() const override { return true; }

    void seek(TagLib::offset_t offset, Position p) override
    {
        const qint64 from = p == Beginning ? 0 : (p == Current ? m_pos : m_parts.size);
        m_pos = std::max<qint64>(0, from + offset);
    }
    TagLib::offset_t tell() const override { return m_pos; }
    TagLib::offset_t length() override { return m_parts.size; }

private:
    QByteArray m_name;
    TagParts m_parts;
    qint64 m_pos = 0;
};

} // namespace

EMSCRIPTEN_BINDINGS(octave_webmusic)
{
    emscripten::function("octaveWebMusicPicked", &onPicked);
    emscripten::function("octaveWebMusicTags", &onTags);
    emscripten::function("octaveWebMusicReady", &onReady);
    emscripten::function("octaveWebMusicDecoded", &onDecoded);
}

void WebMusicLibrary::pick(const QStringList &audioExtensions,
                           std::function<void(const QString &, int)> imported)
{
    octave_webmusic_install();
    g_extensions = audioExtensions;
    g_imported = std::move(imported);
    helper().call<void>("pick");
}

QUrl WebMusicLibrary::sourceUrl(const QString &filePath)
{
    int generation = 0, index = 0;
    if (readPlaceholder(filePath, &generation, &index)) {
        if (generation == shared().generation) {   // written on this (main) thread only
            const std::string url = helper().call<std::string>("url", index);
            if (!url.empty())
                return QUrl(QString::fromStdString(url));
        }
        qCWarning(lcWebMusic) << "No browser file behind" << filePath;
    }
    return QUrl::fromLocalFile(filePath);
}

std::unique_ptr<TagLib::IOStream> WebMusicLibrary::tagStream(const QString &filePath)
{
    int generation = 0, index = 0;
    if (!readPlaceholder(filePath, &generation, &index))
        return nullptr;
    Shared &s = shared();
    QMutexLocker lock(&s.mutex);
    const auto it = s.tags.constFind(index);
    if (generation != s.generation || it == s.tags.constEnd())
        return nullptr;
    return std::make_unique<PartsStream>(filePath, *it);
}

bool WebMusicLibrary::decodeMono(const QString &filePath, std::vector<float> *samples, int *sampleRate)
{
    if (QThread::isMainThread()) {
        qCWarning(lcWebMusic) << "decodeMono() on the main thread would wait forever for itself";
        return false;
    }
    int generation = 0, index = 0;
    if (!readPlaceholder(filePath, &generation, &index))
        return false;

    Shared &s = shared();
    QMutexLocker lock(&s.mutex);
    if (generation != s.generation)
        return false;
    const int id = ++s.nextDecode;
    s.decodes.insert(id, {});
    QMetaObject::invokeMethod(QCoreApplication::instance(),
                              [id, index] { helper().call<void>("decode", id, index); }, Qt::QueuedConnection);
    QDeadlineTimer deadline(kDecodeTimeoutMs);
    while (!s.decodes.find(id)->done && !deadline.hasExpired())
        s.decoded.wait(&s.mutex, deadline);
    Decode d = s.decodes.take(id);
    if (!d.done) {
        qCWarning(lcWebMusic) << "The browser took over" << kDecodeTimeoutMs / 1000 << "s to decode" << filePath;
        return false;
    }
    if (d.sampleRate <= 0 || d.samples.empty())
        return false;
    *samples = std::move(d.samples);
    *sampleRate = d.sampleRate;
    return true;
}

qint64 WebMusicLibrary::playerPositionMs()
{
    return qint64(octave_webmusic_position());
}

int WebMusicLibrary::playerPlaying()
{
    return octave_webmusic_playing();
}
