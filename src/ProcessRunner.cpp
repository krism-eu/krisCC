#include "ProcessRunner.h"

#include <QPointer>
#include <QTimer>

#include <signal.h>
#include <unistd.h>

ProcessRunner::ProcessRunner(QObject *parent)
    : QObject(parent)
{
}

ProcessRunner::~ProcessRunner()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        terminateProcess(true);
        m_process->waitForFinished(1000);
    }
}

bool ProcessRunner::running() const
{
    return m_process && m_process->state() != QProcess::NotRunning && !m_finished;
}

void ProcessRunner::appendBounded(QByteArray &target, const QByteArray &data, qsizetype limit)
{
    if (data.isEmpty() || limit <= 0)
        return;
    target += data;
    if (target.size() > limit)
        target = target.right(limit);
}

bool ProcessRunner::start(const Options &options)
{
    if (running() || options.program.isEmpty())
        return false;

    m_options = options;
    m_stdout.clear();
    m_stderr.clear();
    m_cancelRequested = false;
    m_timedOut = false;
    m_finished = false;

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

    connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
        if (!m_process || m_finished)
            return;
        const QByteArray data = m_process->readAllStandardOutput();
        appendBounded(m_stdout, data, m_options.maxOutputBytes);
        if (!data.isEmpty())
            emit outputReady(data);
    });
    connect(m_process, &QProcess::readyReadStandardError, this, [this] {
        if (!m_process || m_finished || m_options.mergedChannels)
            return;
        const QByteArray data = m_process->readAllStandardError();
        appendBounded(m_stderr, data, m_options.maxOutputBytes);
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
        drain();
        if (m_cancelRequested)
            finish(Cancelled, exitCode);
        else if (m_timedOut)
            finish(TimedOut, exitCode);
        else if (status == QProcess::NormalExit && exitCode == 0)
            finish(Success, exitCode);
        else
            finish(ExitError, exitCode);
    });

    m_process->start();

    if (options.timeoutMs > 0) {
        const QPointer<QProcess> guarded(m_process);
        QTimer::singleShot(options.timeoutMs, m_process, [this, guarded] {
            if (!guarded || guarded != m_process || m_finished
                || guarded->state() == QProcess::NotRunning)
                return;
            m_timedOut = true;
            terminateProcess(false);
            QTimer::singleShot(2000, guarded, [this, guarded] {
                if (guarded && guarded == m_process && !m_finished
                    && guarded->state() != QProcess::NotRunning)
                    terminateProcess(true);
            });
        });
    }
    return true;
}

void ProcessRunner::drain()
{
    if (!m_process)
        return;
    const QByteArray out = m_process->readAllStandardOutput();
    appendBounded(m_stdout, out, m_options.maxOutputBytes);
    if (!out.isEmpty())
        emit outputReady(out);
    if (!m_options.mergedChannels)
        appendBounded(m_stderr, m_process->readAllStandardError(), m_options.maxOutputBytes);
}

void ProcessRunner::terminateProcess(bool force)
{
    if (!m_process || m_process->state() == QProcess::NotRunning)
        return;
    const qint64 pid = m_process->processId();
    if (m_options.processGroup && pid > 0
        && ::kill(-pid, force ? SIGKILL : SIGTERM) == 0)
        return;
    if (force)
        m_process->kill();
    else
        m_process->terminate();
}

bool ProcessRunner::cancel()
{
    if (!running())
        return false;
    m_cancelRequested = true;
    terminateProcess(false);
    const QPointer<QProcess> guarded(m_process);
    QTimer::singleShot(2000, guarded, [this, guarded] {
        if (guarded && guarded == m_process && !m_finished
            && guarded->state() != QProcess::NotRunning)
            terminateProcess(true);
    });
    return true;
}

void ProcessRunner::finish(Outcome outcome, int exitCode, const QString &errorString)
{
    if (m_finished)
        return;
    m_finished = true;
    drain();

    QProcess *process = m_process;
    m_process = nullptr;
    const QByteArray standardOutput = m_stdout;
    const QByteArray standardError = m_stderr;
    if (process)
        process->deleteLater();

    emit finished(outcome, exitCode, standardOutput, standardError, errorString);
}
