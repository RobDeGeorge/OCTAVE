// FolderBrowser (src/util/folderbrowser.{h,cpp}): what a remote UI may
// list when choosing the music folder. Inside the roots: sub-folders and
// audio files by name, no dotfiles, a parent up to the root and not past
// it. Outside: refused, whether asked directly, through "..", or through a
// symlink (to a folder outside, listed or followed).

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "util/folderbrowser.h"

namespace {
const QStringList kAudio = {".mp3", ".flac", ".m4a"};
}

class TstFolderBrowser : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        QDir t(m_tmp.path());
        for (const char *d : {"root/Music/Rock", "root/Music/Jazz", "root/.hidden", "outside/secret"})
            QVERIFY(t.mkpath(d));
        for (const char *f : {"root/Music/a.mp3", "root/Music/B.FLAC", "root/Music/notes.txt", "root/Music/.c.mp3",
                              "outside/secret/x.mp3"}) {
            QFile file(t.filePath(f));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        QVERIFY(QFile::link(t.filePath("outside"), t.filePath("root/Music/escape")));
        QVERIFY(QFile::link(t.filePath("root/Music/Jazz"), t.filePath("root/Music/jazzlink")));
        m_root = QFileInfo(t.filePath("root")).canonicalFilePath();
        m_outside = QFileInfo(t.filePath("outside")).canonicalFilePath();
    }

    void rootsWhenNoPath()
    {
        const QVariantList roots = FolderBrowser::list({}, {m_root, m_tmp.filePath("missing"), m_root}, kAudio)
                                       .value("roots").toList();
        QCOMPARE(roots.size(), 1);   // missing dropped, duplicates merged
        QCOMPARE(roots.first().toMap().value("path").toString(), m_root);
        QCOMPARE(roots.first().toMap().value("name").toString(), QStringLiteral("root"));
    }

    void listsFoldersAndAudioByName()
    {
        const QVariantMap r = FolderBrowser::list(m_root + "/Music", {m_root}, kAudio);
        QCOMPARE(r.value("path").toString(), m_root + "/Music");
        QCOMPARE(r.value("parent").toString(), m_root);
        QCOMPARE(r.value("dirs").toStringList(), (QStringList{"Jazz", "jazzlink", "Rock"}));   // no escape link
        QCOMPARE(r.value("audioFiles").toStringList(), (QStringList{"a.mp3", "B.FLAC"}));      // no .txt, no dotfile
        QCOMPARE(r.value("truncated").toBool(), false);
        // At a root: no parent above it
        const QVariantMap top = FolderBrowser::list(m_root, {m_root}, kAudio);
        QCOMPARE(top.value("parent").toString(), QString());
        QCOMPARE(top.value("dirs").toStringList(), QStringList{"Music"});   // .hidden not listed
        // A root inside another root still goes up
        QCOMPARE(FolderBrowser::list(m_root + "/Music", {m_root, m_root + "/Music"}, kAudio).value("parent").toString(), m_root);
    }

    void refusesWaysOut()
    {
        for (const QString &p : {m_outside, m_outside + "/secret", m_root + "/..", m_root + "/Music/../../outside",
                                 m_root + "/Music/escape", m_root + "/Music/escape/secret", QStringLiteral("/"),
                                 QStringLiteral("/etc"), m_root + "/Music/a.mp3", m_root + "/nope"}) {
            const QVariantMap r = FolderBrowser::list(p, {m_root}, kAudio);
            QVERIFY2(r.contains("error"), qPrintable(p));
            QVERIFY2(!r.contains("dirs"), qPrintable(p));
        }
        // A link that stays inside is followed, to its real path
        QCOMPARE(FolderBrowser::list(m_root + "/Music/jazzlink", {m_root}, kAudio).value("path").toString(),
                 m_root + "/Music/Jazz");
        QCOMPARE(FolderBrowser::confine(m_root + "/Music/escape", {m_root}), QString());
    }

private:
    QTemporaryDir m_tmp;
    QString m_root, m_outside;
};

QTEST_GUILESS_MAIN(TstFolderBrowser)
#include "tst_folderbrowser.moc"
