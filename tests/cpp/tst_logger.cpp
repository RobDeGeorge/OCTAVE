// OctaveLog's crash path: what reaches octave-cpp-error.log when the process
// dies. Each case runs this binary again as a child (OCTAVE_LOGGER_CHILD
// says how to die) on a config directory of its own, then reads the log.

#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <csignal>
#include <cstdio>
#include <cstring>
#include <sys/resource.h>

#include "util/logger.h"

// GCC says __SANITIZE_ADDRESS__; Clang answers __has_feature
#if defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    define OCTAVE_CLANG_ASAN 1
#  endif
#endif
#ifndef OCTAVE_CLANG_ASAN
#  define OCTAVE_CLANG_ASAN 0
#endif

namespace {
// Recursion the compiler can neither prove endless nor turn into a loop
volatile int g_deeper = 1;
int recurse(int depth)
{
    volatile char pad[512];
    pad[0] = static_cast<char>(depth);
    if (!g_deeper)
        return pad[0];
    return recurse(depth + 1) + pad[0];
}

int child(const QByteArray &how)
{
    // No core file for a crash that is the point of the test
    const rlimit none{0, 0};
    setrlimit(RLIMIT_CORE, &none);
    OctaveLog::install(false);
    if (how == "overflow")
        return recurse(0);
    if (how == "rotated") {
        // Past the error log's 2 MB: it rotates, and the crash must land in
        // the new file. The lines themselves need not be seen
        if (!std::freopen("/dev/null", "w", stderr))
            return 3;
        const QByteArray filler(200, 'w');
        for (int i = 0; i < 9000; ++i)
            qWarning("%s", filler.constData());
        std::raise(SIGSEGV);
    }
    if (how == "segv")
        std::raise(SIGSEGV);
    if (how == "direct")
        OctaveLog::writeDirect("a line with no lock taken\n");
    if (how == "fatal")
        qFatal("the fatal message");
    return 0;
}
} // namespace

class TstLogger : public QObject
{
    Q_OBJECT

private:
    struct Run {
        QProcess::ExitStatus status = QProcess::NormalExit;
        int code = -1;
        QByteArray errorLog;
    };
    static Run run(const QByteArray &how)
    {
        Run r;
        QTemporaryDir cfg;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("OCTAVE_LOGGER_CHILD"), QString::fromLatin1(how));
        env.insert(QStringLiteral("HOME"), cfg.path());
        env.insert(QStringLiteral("XDG_CONFIG_HOME"), cfg.path() + QStringLiteral("/config"));
        QProcess p;
        p.setProcessEnvironment(env);
        p.setProcessChannelMode(QProcess::ForwardedErrorChannel);
        p.start(QCoreApplication::applicationFilePath(), QStringList());
        if (!p.waitForFinished(30000)) {
            p.kill();
            p.waitForFinished(3000);
        }
        r.status = p.exitStatus();
        r.code = p.exitCode();
        QDirIterator it(cfg.path(), {QStringLiteral("octave-cpp-error.log")}, QDir::Files | QDir::Hidden,
                        QDirIterator::Subdirectories);
        if (it.hasNext()) {
            QFile f(it.next());
            if (f.open(QIODevice::ReadOnly))
                r.errorLog = f.readAll();
        }
        return r;
    }

private slots:
    void aSegfaultLeavesItsNameAndABacktrace()
    {
        const Run r = run("segv");
        QCOMPARE(r.status, QProcess::CrashExit);
        QCOMPARE(r.code, int(SIGSEGV));   // the default action still ran: the OS saw a real crash
        QVERIFY2(r.errorLog.contains("==== FATAL SIGSEGV"), r.errorLog.constData());
        QVERIFY(r.errorLog.contains("==== end ===="));
    }

    // Out of stack there is no room for a handler on it. Before the
    // handlers had a stack of their own this died with an empty log
    void aStackOverflowOnTheMainThreadIsLoggedToo()
    {
#if defined(__SANITIZE_ADDRESS__) || OCTAVE_CLANG_ASAN
        QSKIP("the address sanitizer takes a stack overflow itself");
#endif
        const Run r = run("overflow");
        QCOMPARE(r.status, QProcess::CrashExit);
        QVERIFY2(r.errorLog.contains("==== FATAL SIGSEGV"), r.errorLog.constData());
        QVERIFY(r.errorLog.contains("==== end ===="));
    }

    // The handler's descriptor follows the error log through a rotation,
    // under the same number
    void aCrashAfterARotationIsInTheNewLog()
    {
        const Run r = run("rotated");
        QCOMPARE(r.status, QProcess::CrashExit);
        QVERIFY2(r.errorLog.size() < 1024 * 1024, "the error log did not rotate");
        QVERIFY2(r.errorLog.contains("==== FATAL SIGSEGV"), r.errorLog.right(400).constData());
        QVERIFY(r.errorLog.contains("==== end ===="));
    }

    void aFatalMessageIsFollowedByTheAbort()
    {
        const Run r = run("fatal");
        QCOMPARE(r.status, QProcess::CrashExit);
        const int message = r.errorLog.indexOf("the fatal message");
        QVERIFY2(message >= 0, r.errorLog.constData());
        QVERIFY(r.errorLog.indexOf("==== FATAL SIGABRT") > message);
    }

    // The watchdog's last line before it aborts: straight to the error log
    void aDirectLineReachesTheErrorLog()
    {
        const Run r = run("direct");
        QCOMPARE(r.status, QProcess::NormalExit);
        QCOMPARE(r.code, 0);
        QVERIFY2(r.errorLog.contains("a line with no lock taken\n"), r.errorLog.constData());
    }
};

int main(int argc, char **argv)
{
    const QByteArray how = qgetenv("OCTAVE_LOGGER_CHILD");
    if (!how.isEmpty())
        return child(how);
    QCoreApplication app(argc, argv);
    TstLogger test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_logger.moc"
