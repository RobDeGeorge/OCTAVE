// The download page's two lists are models on DownloadManager (it used to
// JSON.parse a results string, patch it field by field from a second JSON
// signal and rebuild the queue from four signals): a status change is one
// dataChanged on one row, so the list keeps its delegates and its scroll
// position, and the queue follows each task's state.

#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include "managers/downloadmanager.h"

namespace {
const char *kTwoHits =
    R"({"id":"aaa111","title":"Song A","artist":"Band","album":"LP","duration":200,"thumbnail":"https://x/a.jpg"})"
    "\n"
    R"({"id":"bbb222","title":"Song B","artist":"Band","album":"LP","duration":180,"thumbnail":"https://x/b.jpg"})"
    "\n";

QVariant role(const QAbstractItemModel *m, int row, const char *name)
{
    return m->data(m->index(row, 0), m->roleNames().key(name));
}
} // namespace

class TstDownloadManager : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_tmp = std::make_unique<QTemporaryDir>();
        QVERIFY(m_tmp->isValid());
        qputenv("HOME", m_tmp->path().toUtf8());
        qputenv("XDG_CONFIG_HOME", (m_tmp->path() + "/config").toUtf8());
    }

    void searchHitsAreRows()
    {
        DownloadManager dm;
        QAbstractItemModelTester tester(dm.searchResults(), QAbstractItemModelTester::FailureReportingMode::QtTest);
        publish(dm);
        QAbstractListModel *m = dm.searchResults();
        QCOMPARE(m->rowCount(), 2);
        QCOMPARE(m->property("count").toInt(), 2);
        QCOMPARE(role(m, 0, "song_id").toString(), QStringLiteral("aaa111"));
        QCOMPARE(role(m, 1, "name").toString(), QStringLiteral("Song B"));
        QCOMPARE(role(m, 0, "is_downloaded").toBool(), false);
        QVERIFY(!role(m, 0, "is_downloading").toBool());
        QCOMPARE(dm.property("searchResults").value<QAbstractListModel *>(), m);
    }

    void aPatchChangesOneRowInPlace()
    {
        DownloadManager dm;
        publish(dm);
        QAbstractListModel *m = dm.searchResults();
        QSignalSpy changed(m, &QAbstractItemModel::dataChanged);
        QSignalSpy reset(m, &QAbstractItemModel::modelReset);
        dm._emitStatusPatch(QStringLiteral("bbb222"), QStringLiteral("is_failed"), true);
        QCOMPARE(reset.count(), 0);   // the view keeps its delegates and scroll
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.first().at(0).toModelIndex().row(), 1);
        QVERIFY(role(m, 1, "is_failed").toBool());
        // Same value again: nothing
        dm._emitStatusPatch(QStringLiteral("bbb222"), QStringLiteral("is_failed"), true);
        QCOMPARE(changed.count(), 1);
    }

    void theQueueFollowsEachTask()
    {
        DownloadManager dm;
        QAbstractItemModelTester tester(dm.downloadQueue(), QAbstractItemModelTester::FailureReportingMode::QtTest);
        publish(dm);
        QAbstractListModel *q = dm.downloadQueue();
        QAbstractListModel *m = dm.searchResults();

        DownloadTask t;
        t.songId = QStringLiteral("aaa111");
        t.songName = QStringLiteral("Song A");
        t.artist = QStringLiteral("Band");
        t.status = QStringLiteral("queued");
        dm.m_downloadTasks.insert(t.songId, t);
        dm._syncTask(t.songId);
        QCOMPARE(q->rowCount(), 1);
        QCOMPARE(role(q, 0, "songName").toString(), QStringLiteral("Band - Song A"));
        QCOMPARE(role(q, 0, "status").toString(), QStringLiteral("downloading"));   // queued shows as downloading
        QVERIFY(role(m, 0, "is_downloading").toBool());

        dm.m_downloadTasks[t.songId].progress = 0.5f;
        dm._syncTask(t.songId);
        QCOMPARE(q->rowCount(), 1);
        QCOMPARE(role(q, 0, "progress").toDouble(), 0.5);
        QCOMPARE(role(m, 0, "progress").toDouble(), 0.5);

        auto &task = dm.m_downloadTasks[t.songId];
        task.status = QStringLiteral("complete");
        task.filePath = m_tmp->path() + "/Band - Song A.m4a";
        dm._syncTask(t.songId);
        QCOMPARE(role(q, 0, "status").toString(), QStringLiteral("complete"));
        QCOMPARE(role(q, 0, "filePath").toString(), task.filePath);
        QVERIFY(!role(m, 0, "is_downloading").toBool());
        QCOMPARE(role(m, 0, "file_path").toString(), task.filePath);

        // A cancelled one is "failed" with its reason
        DownloadTask c = t;
        c.songId = QStringLiteral("bbb222");
        c.status = QStringLiteral("cancelled");
        c.errorMessage = QStringLiteral("Download cancelled");
        dm.m_downloadTasks.insert(c.songId, c);
        dm._syncTask(c.songId);
        QCOMPARE(role(q, 1, "status").toString(), QStringLiteral("failed"));
        QCOMPARE(role(q, 1, "errorMsg").toString(), QStringLiteral("Download cancelled"));

        // Clear finished: both go, and their tasks with them
        dm.clear_finished_downloads();
        QCOMPARE(q->rowCount(), 0);
        QVERIFY(dm.m_downloadTasks.isEmpty());
    }

    void aNewSearchShowsRunningDownloads()
    {
        DownloadManager dm;
        DownloadTask t;
        t.songId = QStringLiteral("bbb222");
        t.status = QStringLiteral("downloading");
        t.progress = 0.25f;
        dm.m_downloadTasks.insert(t.songId, t);
        publish(dm);
        QVERIFY(role(dm.searchResults(), 1, "is_downloading").toBool());
        QCOMPARE(role(dm.searchResults(), 1, "progress").toDouble(), 0.25);
    }

private:
    static void publish(DownloadManager &dm)
    {
        dm.m_allResults = dm._parseSearchOutput(kTwoHits);
        dm._markDownloaded(dm.m_allResults);
        dm._publishResults();
    }

    std::unique_ptr<QTemporaryDir> m_tmp;
};

QTEST_MAIN(TstDownloadManager)
#include "tst_downloadmanager.moc"
