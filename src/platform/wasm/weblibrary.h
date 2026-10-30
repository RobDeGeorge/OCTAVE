#ifndef OCTAVE_WASM_WEBLIBRARY_H
#define OCTAVE_WASM_WEBLIBRARY_H

// The music library in the browser demo (WebAssembly build only).
//
// A page can't list the visitor's disk: QDir only sees Emscripten's in-memory
// filesystem, and a QML FolderDialog would browse that (empty) filesystem.
// So the library is a folder the visitor picks in the browser's own folder
// picker (<input type=file webkitdirectory>: Chrome, Edge, Firefox, Safari),
// for this visit only (the page keeps nothing between visits).
//
// The songs are never copied into wasm memory whole. Every audio file of the
// picked folder gets a tiny placeholder at the same relative path under
// /weblibrary/<n>/, so MediaManager scans, renames and moves the library
// with its ordinary QDir code; the placeholder holds the index of the
// browser File it stands for, and through it:
//   sourceUrl()   the blob: URL the player plays (URL.createObjectURL, no copy)
//   tagStream()   what TagLib reads: the parts of the file that hold tags and
//                 covers (ID3v2 + the first 64 KB of audio, FLAC's metadata
//                 blocks, an MP4's moov atom, the last 64 KB), read at pick
//                 time; zeros in between. ~0.1-0.5 MB a song, at most 512 MB.
//   decodeMono()  the track decoded by the browser (Web Audio) for the
//                 visualizer, which has no QAudioDecoder in the browser.

#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>
#include <memory>
#include <vector>

namespace TagLib { class IOStream; }

namespace WebMusicLibrary {

// Opens the browser's folder picker; call from a tap (browsers only open a
// picker during a user gesture). When the visitor has picked a folder and
// its tags are read, `imported` gets the library root to scan and how many
// audio files (by `audioExtensions`, lower case with the dot) it holds.
// The library picked before stays playable until `imported` returns (and
// for good if the new pick holds no audio, songCount 0: don't switch to it).
// Nothing happens if the picker is cancelled. Main thread only.
void pick(const QStringList &audioExtensions,
          std::function<void(const QString &root, int songCount)> imported);

// The URL to play `filePath` from: a blob: URL for a placeholder pick()
// made, else the file URL of the path itself. Main thread only.
QUrl sourceUrl(const QString &filePath);

// The tag parts of the file a placeholder stands for, as a read-only TagLib
// stream named `filePath`; null for any other path. Any thread.
std::unique_ptr<TagLib::IOStream> tagStream(const QString &filePath);

// The file a placeholder stands for, decoded by the browser to mono at
// 8 kHz. Blocks until the page has decoded it (on the main thread), so call
// it from a worker thread, never the main thread. False for any other path
// or if the browser can't decode it.
bool decodeMono(const QString &filePath, std::vector<float> *samples, int *sampleRate);

// Where Qt's player is in the track, and whether it is playing (1 / 0),
// from its <audio> element (the last one on the page: Qt appends it on every
// setSource); -1 without one. For audio in the browser Qt's own position()
// is 0 and its playbackState() StoppedState while it plays. Main thread only.
qint64 playerPositionMs();
int playerPlaying();

// Sets the volume (0..1) of the player's <audio> elements, including the ones
// Qt makes later; Qt leaves a new one at full volume. Main thread only.
void setPlayerVolume(float volume);

} // namespace WebMusicLibrary

#endif
