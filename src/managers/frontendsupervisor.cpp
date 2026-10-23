#include "frontendsupervisor.h"

#include "../service/protocol.h"
#include "../service/servicepairing.h"

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QStandardPaths>

#if QT_CONFIG(process)
#include <QProcess>
#endif

Q_LOGGING_CATEGORY(lcSupervisor, "octave.frontends.supervisor")

FrontendSupervisor::FrontendSupervisor(const FrontendManifest &manifest, const QString &dir, ServicePairing *pairing,
                                       quint16 port, QObject *parent)
    : QObject(parent)
    , m_manifest(manifest)
    , m_dir(dir)
    , m_pairing(pairing)
    , m_port(port)
{
    m_restart.setSingleShot(true);
    connect(&m_restart, &QTimer::timeout, this, &FrontendSupervisor::launch);
}

FrontendSupervisor::~FrontendSupervisor()
{
    stop();
}

QStringList FrontendSupervisor::resolveCommand(const FrontendManifest &manifest, const QString &dir, QString *error)
{
    QStringList argv = manifest.command();
    if (argv.isEmpty()) {
        *error = QStringLiteral("no program for %1").arg(FrontendManifest::currentPlatform());
        return {};
    }
    const QString program = argv.first();
    const QFileInfo inside(QDir(dir).filePath(program));
    if (inside.isFile()) {
        argv[0] = inside.absoluteFilePath();
    } else if (!program.contains(QLatin1Char('/'))) {
        const QString found = QStandardPaths::findExecutable(program);
        if (found.isEmpty()) {
            *error = QStringLiteral("%1 is neither in the frontend nor on PATH").arg(program);
            return {};
        }
        argv[0] = found;
    } else {
        *error = QStringLiteral("%1 is not in the frontend").arg(program);
        return {};
    }
    for (int i = 1; i < argv.size(); ++i)
        argv[i].replace(QLatin1String("{dir}"), QDir(dir).absolutePath());
    return argv;
}

bool FrontendSupervisor::running() const
{
#if QT_CONFIG(process)
    return m_process && m_process->state() != QProcess::NotRunning;
#else
    return false;
#endif
}

void FrontendSupervisor::start()
{
    m_stopping = false;
    m_quickCrashes = 0;
    launch();
}

void FrontendSupervisor::revokeToken()
{
    if (!m_tokenId.isEmpty() && m_pairing)
        m_pairing->revoke(m_tokenId);
    m_tokenId.clear();
}

void FrontendSupervisor::launch()
{
#if !QT_CONFIG(process)
    emit gaveUp(QStringLiteral("this build can't start programs"));
#else
    if (m_stopping)
        return;
    QString error;
    const QStringList argv = resolveCommand(m_manifest, m_dir, &error);
    if (argv.isEmpty()) {
        // Nothing a restart would fix
        qCWarning(lcSupervisor).noquote() << m_manifest.id << ":" << error;
        emit gaveUp(QStringLiteral("%1 can't start: %2").arg(m_manifest.id, error));
        return;
    }

    revokeToken();
    const QString token = m_pairing ? m_pairing->addSessionDevice(QStringLiteral("Frontend: %1").arg(m_manifest.name),
                                                                  m_manifest.scopes, &m_tokenId)
                                    : QString();
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("OCTAVE_HOST"), QStringLiteral("127.0.0.1"));
    env.insert(QStringLiteral("OCTAVE_PORT"), QString::number(m_port));
    env.insert(QStringLiteral("OCTAVE_TOKEN"), token);
    env.insert(QStringLiteral("OCTAVE_PROTOCOL"), QString::number(OctaveService::kProtocolVersion));
    env.insert(QStringLiteral("OCTAVE_SCOPES"), m_manifest.scopes.join(QLatin1Char(',')));
    env.insert(QStringLiteral("OCTAVE_FRONTEND_DIR"), QDir(m_dir).absolutePath());

    delete m_process;
    m_process = new QProcess(this);
    m_process->setProcessEnvironment(env);
    m_process->setWorkingDirectory(m_dir);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    const QString id = m_manifest.id;
    QProcess *p = m_process;
    connect(p, &QProcess::readyRead, this, [p, id] {
        while (p->canReadLine())
            qCInfo(lcSupervisor).noquote() << QStringLiteral("[%1]").arg(id) << QString::fromUtf8(p->readLine(4096)).trimmed();
        if (p->bytesAvailable() > 4096)   // a long line without a newline
            qCInfo(lcSupervisor).noquote() << QStringLiteral("[%1]").arg(id) << QString::fromUtf8(p->read(4096));
    });
    connect(p, &QProcess::finished, this, [this, p](int code, QProcess::ExitStatus status) {
        if (p != m_process)
            return;
        finished(code, status == QProcess::CrashExit, status == QProcess::CrashExit ? QStringLiteral("crashed")
                                                                                     : QStringLiteral("exited with %1").arg(code));
    });
    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError e) {
        if (p == m_process && e == QProcess::FailedToStart)
            finished(-1, true, QStringLiteral("failed to start (%1)").arg(p->errorString()));
    });
    ++m_launches;
    m_runTime.start();
    qCInfo(lcSupervisor).noquote() << "starting" << id << m_manifest.version << ":" << argv.join(QLatin1Char(' '))
                                   << "with" << m_manifest.scopes.join(QLatin1Char(','));
    connect(p, &QProcess::started, this, [this, p] { emit started(p->processId()); });
    p->start(argv.first(), argv.mid(1));
#endif
}

void FrontendSupervisor::finished(int code, bool crashed, const QString &what)
{
    if (m_stopping)
        return;
    revokeToken();
    const qint64 ran = m_runTime.isValid() ? m_runTime.elapsed() : 0;
    if (!crashed && code == 0) {
        qCInfo(lcSupervisor).noquote() << m_manifest.id << "quit (exit code 0): OCTAVE quits with it";
        emit exitedCleanly();
        return;
    }
    if (ran >= m_options.quickRunMs)
        m_quickCrashes = 0;
    ++m_quickCrashes;
    if (m_quickCrashes >= m_options.maxQuickCrashes) {
        const QString reason = QStringLiteral("%1 %2, %3 times in a row within %4 s of starting")
                                   .arg(m_manifest.id, what).arg(m_quickCrashes).arg(m_options.quickRunMs / 1000);
        qCWarning(lcSupervisor).noquote() << "giving up:" << reason;
        emit gaveUp(reason);
        return;
    }
    const int backoff = qMin(m_options.maxBackoffMs, m_options.firstBackoffMs << qMin(m_quickCrashes - 1, 16));
    qCWarning(lcSupervisor).noquote() << m_manifest.id << what << "after" << ran << "ms; starting it again in" << backoff << "ms";
    m_restart.start(backoff);
}

void FrontendSupervisor::stop()
{
    m_stopping = true;
    m_restart.stop();
#if QT_CONFIG(process)
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        if (!m_process->waitForFinished(m_options.stopGraceMs)) {
            qCWarning(lcSupervisor).noquote() << m_manifest.id << "did not quit within" << m_options.stopGraceMs << "ms; killed";
            m_process->kill();
            m_process->waitForFinished(1000);
        }
    }
#endif
    revokeToken();
}
