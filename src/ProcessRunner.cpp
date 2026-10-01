#include "ProcessRunner.h"

#include <QDir>
#include <QFile>
#include <QPointer>

#include <cerrno>
#include <signal.h>
#include <unistd.h>

namespace {
using ProcessMap = QHash<qint64, quint64>;

ProcessMap processesInGroup(qint64 pgid)
{
    ProcessMap result;
    if (pgid <= 0)
        return result;

    const QDir proc(QStringLiteral("/proc"));
    const QStringList entries = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &entry : entries) {
        bool pidOk = false;
        const qint64 pid = entry.toLongLong(&pidOk);
        if (!pidOk || pid <= 0)
            continue;

        QFile statFile(proc.filePath(entry + QStringLiteral("/stat")));
        if (!statFile.open(QIODevice::ReadOnly))
            continue;
        const QByteArray line = statFile.readAll().trimmed();
        const qsizetype closeParen = line.lastIndexOf(')');
        if (closeParen < 0 || closeParen + 2 >= line.size())
            continue;

        const QList<QByteArray> fields = line.mid(closeParen + 2).split(' ');
        if (fields.size() <= 19)
            continue;

        bool groupOk = false;
        bool startOk = false;
        const qint64 processGroup = fields.at(2).toLongLong(&groupOk);
        const quint64 startTime = fields.at(19).toULongLong(&startOk);
        if (groupOk && startOk && processGroup == pgid)
            result.insert(pid, startTime);
    }
    return result;
}

bool mapsShareIdentity(const ProcessMap &known, const ProcessMap &current)
{
    for (auto it = current.cbegin(); it != current.cend(); ++it) {
        const auto knownIt = known.constFind(it.key());
        if (knownIt != known.cend() && knownIt.value() == it.value())
            return true;
    }
    return false;
}
}

ProcessRunner::ProcessRunner(QObject *parent)
    : QObject(parent)
{
    m_groupTracker.setInterval(100);
    m_groupTracker.setSingleShot(false);
    connect(&m_groupTracker, &QTimer::timeout, this, [this] {
        if (!m_active || !m_options.processGroup) {
            m_groupTracker.stop();
            return;
        }
        refreshTrackedGroup(false);
        if (m_leaderExited && !trackedGroupAlive())
            finalizePendingOutcome();
    });
}

ProcessRunner::~ProcessRunner()
{
    ++m_generation;
    m_groupTracker.stop();

    if (!m_active)
        return;

    if (m_options.processGroup && m_processGroupId > 0) {
        refreshTrackedGroup(!m_leaderExited);
        if (signalTrackedGroup(SIGTERM)) {
            for (int i = 0; i < 10 && trackedGroupAlive(); ++i)
                ::usleep(100 * 1000);
            if (trackedGroupAlive())
                (void)signalTrackedGroup(SIGKILL);
        }
    }

    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

bool ProcessRunner::running() const
{
    return m_active && !m_finished;
}

void ProcessRunner::appendBounded(QByteArray &target, const QByteArray &data, qsizetype limit)
{
    if (data.isEmpty() || limit <= 0)
        return;
    target += data;
    if (target.size() > limit)
        target = target.right(limit);
}

void ProcessRunner::appendStdout(const QByteArray &data)
{
    if (!data.isEmpty() && m_options.maxOutputBytes > 0
        && m_stdout.size() + data.size() > m_options.maxOutputBytes) {
        m_stdoutTruncated = true;
    }
    appendBounded(m_stdout, data, m_options.maxOutputBytes);
}

void ProcessRunner::appendStderr(const QByteArray &data)
{
    if (!data.isEmpty() && m_options.maxOutputBytes > 0
        && m_stderr.size() + data.size() > m_options.maxOutputBytes) {
        m_stderrTruncated = true;
    }
    appendBounded(m_stderr, data, m_options.maxOutputBytes);
}

bool ProcessRunner::start(const Options &options)
{
    if (running() || options.program.isEmpty())
        return false;

    ++m_generation;
    m_options = options;
    m_stdout.clear();
    m_stderr.clear();
    m_groupMembers.clear();
    m_processGroupId = -1;
    m_pendingExitCode = -1;
    m_pendingOutcome = ExitError;
    m_pendingErrorString.clear();
    m_cancelRequested = false;
    m_timedOut = false;
    m_finished = false;
    m_active = true;
    m_leaderExited = false;
    m_groupWasForced = false;
    m_killScheduled = false;
    m_stdoutTruncated = false;
    m_stderrTruncated = false;

    m_process = new QProcess(this);
    m_process->setProgram(options.program);
    m_process->setArguments(options.arguments);
    m_process->setStandardInputFile(QProcess::nullDevice());
    if (!options.workingDirectory.isEmpty())
        m_process->setWorkingDirectory(options.workingDirectory);
    m_process->setProcessChannelMode(options.mergedChannels
                                         ? QProcess::MergedChannels
                                         : QProcess::SeparateChannels);
    if (options.processGroup) {
        m_process->setChildProcessModifier([] {
            (void)::setsid();
        });
    }

    connect(m_process, &QProcess::started, this, [this] {
        if (!m_process || !m_active)
            return;
        if (m_options.processGroup) {
            m_processGroupId = m_process->processId();
            refreshTrackedGroup(true);
            if (!m_groupTracker.isActive())
                m_groupTracker.start();
        }
    });
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
        if (!m_process || m_finished)
            return;
        const QByteArray data = m_process->readAllStandardOutput();
        appendStdout(data);
        if (!data.isEmpty())
            emit outputReady(data);
    });
    connect(m_process, &QProcess::readyReadStandardError, this, [this] {
        if (!m_process || m_finished || m_options.mergedChannels)
            return;
        appendStderr(m_process->readAllStandardError());
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (!m_process || m_finished || error != QProcess::FailedToStart)
            return;
        finish(FailedToStart, -1, m_process->errorString());
    });
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus status) {
        if (!m_process || m_finished)
            return;
        handleLeaderFinished(exitCode, status);
    });

    m_process->start();

    if (options.timeoutMs > 0) {
        const quint64 generation = m_generation;
        QTimer::singleShot(options.timeoutMs, this, [this, generation] {
            if (generation != m_generation || !m_active || m_finished)
                return;

            const bool leaderRunning = m_process
                && m_process->state() != QProcess::NotRunning;
            const bool descendantsRunning = m_options.processGroup && trackedGroupAlive();
            if (!leaderRunning && !descendantsRunning)
                return;

            m_timedOut = true;
            if (m_leaderExited) {
                m_pendingOutcome = TimedOut;
                m_pendingErrorString.clear();
            }
            requestGroupTermination();
        });
    }
    return true;
}

void ProcessRunner::drain()
{
    if (!m_process)
        return;
    const QByteArray out = m_process->readAllStandardOutput();
    appendStdout(out);
    if (!out.isEmpty())
        emit outputReady(out);
    if (!m_options.mergedChannels)
        appendStderr(m_process->readAllStandardError());
}

void ProcessRunner::refreshTrackedGroup(bool allowSeed)
{
    if (!m_options.processGroup || m_processGroupId <= 0)
        return;

    const ProcessMap current = processesInGroup(m_processGroupId);
    if (current.isEmpty())
        return;

    bool continuity = mapsShareIdentity(m_groupMembers, current);
    if (!continuity && allowSeed && m_groupMembers.isEmpty())
        continuity = true;

    if (!continuity && !m_leaderExited && m_process
        && m_process->state() != QProcess::NotRunning
        && m_process->processId() == m_processGroupId) {
        continuity = true;
    }

    if (!continuity)
        return;

    for (auto it = current.cbegin(); it != current.cend(); ++it)
        m_groupMembers.insert(it.key(), it.value());
}

bool ProcessRunner::trackedGroupAlive()
{
    if (!m_options.processGroup || m_processGroupId <= 0 || m_groupMembers.isEmpty())
        return false;

    const ProcessMap current = processesInGroup(m_processGroupId);
    return mapsShareIdentity(m_groupMembers, current);
}

bool ProcessRunner::signalTrackedGroup(int signalNumber)
{
    if (!m_options.processGroup || m_processGroupId <= 0)
        return false;

    refreshTrackedGroup(!m_leaderExited);
    if (!trackedGroupAlive())
        return false;

    errno = 0;
    return ::kill(-m_processGroupId, signalNumber) == 0;
}

void ProcessRunner::scheduleGroupKill()
{
    if (m_killScheduled)
        return;
    m_killScheduled = true;
    const quint64 generation = m_generation;
    QTimer::singleShot(2000, this, [this, generation] {
        if (generation != m_generation || !m_active || m_finished)
            return;
        m_killScheduled = false;

        if (m_options.processGroup) {
            if (trackedGroupAlive())
                (void)signalTrackedGroup(SIGKILL);
        } else if (m_process && m_process->state() != QProcess::NotRunning) {
            m_process->kill();
        }

        if (m_leaderExited && (!m_options.processGroup || !trackedGroupAlive()))
            finalizePendingOutcome();
    });
}

void ProcessRunner::requestGroupTermination()
{
    if (!m_active || m_finished)
        return;

    bool signalled = false;
    if (m_options.processGroup)
        signalled = signalTrackedGroup(SIGTERM);

    if (!signalled && m_process && m_process->state() != QProcess::NotRunning)
        m_process->terminate();

    scheduleGroupKill();
}

void ProcessRunner::handleLeaderFinished(int exitCode, QProcess::ExitStatus status)
{
    drain();
    m_leaderExited = true;
    m_pendingExitCode = exitCode;

    if (m_cancelRequested)
        m_pendingOutcome = Cancelled;
    else if (m_timedOut)
        m_pendingOutcome = TimedOut;
    else if (status == QProcess::NormalExit && exitCode == 0)
        m_pendingOutcome = Success;
    else
        m_pendingOutcome = ExitError;

    if (!m_options.processGroup) {
        finalizePendingOutcome();
        return;
    }

    refreshTrackedGroup(false);
    if (!trackedGroupAlive()) {
        finalizePendingOutcome();
        return;
    }

    if (!m_groupTracker.isActive())
        m_groupTracker.start();

    if (m_cancelRequested || m_timedOut) {
        requestGroupTermination();
        return;
    }

    const quint64 generation = m_generation;
    QTimer::singleShot(500, this, [this, generation] {
        if (generation != m_generation || !m_active || m_finished || !m_leaderExited)
            return;
        if (!trackedGroupAlive()) {
            finalizePendingOutcome();
            return;
        }
        m_groupWasForced = true;
        (void)signalTrackedGroup(SIGTERM);
        scheduleGroupKill();
    });
}

void ProcessRunner::finalizePendingOutcome()
{
    if (!m_active || m_finished || !m_leaderExited)
        return;
    if (m_options.processGroup && trackedGroupAlive())
        return;

    Outcome outcome = m_pendingOutcome;
    QString errorString = m_pendingErrorString;
    if (outcome == Success && m_groupWasForced) {
        outcome = ExitError;
        if (errorString.isEmpty())
            errorString = tr("Il comando ha lasciato processi discendenti attivi; sono stati arrestati.");
    }
    finish(outcome, m_pendingExitCode, errorString);
}

bool ProcessRunner::cancel()
{
    if (!running())
        return false;
    m_cancelRequested = true;
    if (m_leaderExited)
        m_pendingOutcome = Cancelled;
    requestGroupTermination();
    return true;
}

void ProcessRunner::finish(Outcome outcome, int exitCode, const QString &errorString)
{
    if (m_finished)
        return;
    m_finished = true;
    m_active = false;
    m_groupTracker.stop();
    drain();

    QProcess *process = m_process;
    m_process = nullptr;
    const QByteArray standardOutput = m_stdout;
    const QByteArray standardError = m_stderr;
    m_groupMembers.clear();
    m_processGroupId = -1;
    if (process)
        process->deleteLater();

    emit finished(outcome, exitCode, standardOutput, standardError, errorString);
}
