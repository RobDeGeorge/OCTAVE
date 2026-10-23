#ifndef FRONTENDMANIFEST_H
#define FRONTENDMANIFEST_H

// octave-frontend.json: what an installable frontend says about itself
// (TODO/frontend-platform.md step 4, wiki/writing-a-frontend.html#install).
//
//   {
//     "id": "com.example.dash",          // [a-z0-9][a-z0-9._-]{1,63}, its folder name
//     "name": "Example Dash",
//     "version": "1.2.0",
//     "description": "...",               // optional
//     "protocol": [2, 2],                 // the protocol versions it speaks, min and max
//     "scopes": ["read", "media"],        // the permissions it asks for (servicescopes.h)
//     "web":  {"root": "dist"},           // a folder with index.html, served at GET /
//     "exec": {"linux":   ["bin/dash", "--fullscreen"],   // or a program and its arguments
//              "windows": ["dash.exe"],                   // per platform; at least one
//              "macos":   ["bin/dash"]}
//   }
//
// Exactly one of `web` and `exec`. Paths are relative to the frontend's
// folder and may not leave it. An exec program is either inside the folder
// ("bin/dash") or a bare name looked up on PATH ("python3"); `{dir}` in an
// argument is replaced by the folder. Unknown keys are ignored, so a newer
// manifest still installs. Pure parsing, no file access beyond what
// validateFiles() is given.

#include <QDir>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariantMap>

struct FrontendManifest {
    static constexpr const char *kFileName = "octave-frontend.json";
    static constexpr qint64 kMaxBytes = 64 * 1024;

    QString id;
    QString name;
    QString version;
    QString description;
    int protocolMin = 0;
    int protocolMax = 0;
    QStringList scopes;
    QString webRoot;                     // non-empty for a web frontend
    QMap<QString, QStringList> exec;     // platform ("linux", "windows", "macos") -> argv

    bool isWeb() const { return !webRoot.isEmpty(); }
    bool isExec() const { return !exec.isEmpty(); }
    bool speaks(int protocol) const { return protocol >= protocolMin && protocol <= protocolMax; }

    // The platform key of this build ("linux", "windows", "macos", or "" where
    // exec frontends cannot run: Android, iOS, the browser)
    static QString currentPlatform();
    // This platform's argv, or empty
    QStringList command() const { return exec.value(currentPlatform()); }

    // The manifest in `json`, or one with an empty id and *errors filled
    static FrontendManifest parse(const QByteArray &json, QStringList *errors);
    // The checks that need the unpacked folder: the web root holds an
    // index.html, an exec program inside the folder exists. Errors appended
    static bool validateFiles(const FrontendManifest &m, const QDir &dir, QStringList *errors);
    // A relative path that stays inside its folder: no "..", no absolute or
    // drive paths, no backslashes, no empty or "." parts
    static bool safeRelativePath(const QString &path);

    // For lists and the service: {id, name, version, description, kind,
    // protocol, scopes, command}
    QVariantMap toMap() const;
};

#endif // FRONTENDMANIFEST_H
