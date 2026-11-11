// The update check of a packaged build (AppImage, Windows installer, .dmg,
// APK): no git checkout to compare with origin/main, so NetworkManager asks
// GitHub for the latest release and compares its tag with the version this
// binary was built as. The two pure halves live here so a QtTest can hold
// them (tests/cpp/tst_releasecheck.cpp).

#ifndef RELEASECHECK_H
#define RELEASECHECK_H

#include <QByteArray>
#include <QString>

namespace ReleaseCheck {

// Order of two version strings: negative when a is older than b, 0 when they
// are the same release, positive when a is newer. A leading "v" is ignored,
// missing parts count as 0 ("0.9" == "0.9.0") and a pre-release suffix sorts
// before its release ("0.9.6-rc2" < "0.9.6", "0.9.6-rc2" < "0.9.6-rc10").
int compareVersions(const QString &a, const QString &b);

struct Release {
    bool valid = false;
    QString version;   // the tag without its "v": "0.9.6"
    QString tag;       // "v0.9.6"
    QString name;      // the release's title, may be empty
    QString url;       // its page on github.com (never anywhere else)
};

// The answer of GET /repos/<repo>/releases/latest. Not valid when it is not a
// release object with a version tag, or when it is a draft or a pre-release.
// The url is the release's html_url only when it is a page of <repo> on
// github.com; anything else is replaced by the repo's releases page, since
// the About page opens it in the browser.
Release parseLatest(const QByteArray &json, const QString &repo);

} // namespace ReleaseCheck

#endif // RELEASECHECK_H
