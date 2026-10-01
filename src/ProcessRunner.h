#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>

class ProcessRunner final : public QObject
{
    Q_OBJECT

public:
    enum Outcome {
        Success,
        ExitError,
        FailedToStart,
        TimedOut,
        Cancelled
    };
    Q_ENUM(Outcome)

    struct Options {
        QString program;
        QStringList arguments;
        QString workingDirectory;
        int timeoutMs = 0;
        qsizetype maxOutputBytes = 256 * 1024;
        bool mergedChannels = true;
        bool processGroup = true;
    };

    explicit ProcessRunner(QObject *parent = nullptr);
    ~ProcessRunner() override;

    bool start(const Options &options);
    bool cancel();
    bool running() const;
    bool outputTruncated() const { return m_stdoutTruncated || m_stderrTruncated; }

signals:
    void outputReady(const QByteArray &data);
    void finished(ProcessRunner::Outcome outcome, int exitCode,
                  const QByteArray &standardOutput,
                  const QByteArray &standardError,
                  const QString &errorString);

private:
    void drain();
    void finish(Outcome outcome, int exitCode, const QString &errorString = QString());
    void handleLeaderFinished(int exitCode, QProcess::ExitStatus status);
    void refreshTrackedGroup(bool allowSeed = false);
    bool trackedGroupAlive();
    bool signalTrackedGroup(int signalNumber);
    void requestGroupTermination();
    void scheduleGroupKill();
    void finalizePendingOutcome();
    void appendStdout(const QByteArray &data);
    void appendStderr(const QByteArray &data);
    static void appendBounded(QByteArray &target, const QByteArray &data, qsizetype limit);

    QProcess *m_process = nullptr;
    QTimer m_groupTracker;
    Options m_options;
    QByteArray m_stdout;
    QByteArray m_stderr;
    QHash<qint64, quint64> m_groupMembers;
    qint64 m_processGroupId = -1;
    quint64 m_generation = 0;
    int m_pendingExitCode = -1;
    Outcome m_pendingOutcome = ExitError;
    QString m_pendingErrorString;
    bool m_cancelRequested = false;
    bool m_timedOut = false;
    bool m_finished = true;
    bool m_active = false;
    bool m_leaderExited = false;
    bool m_groupWasForced = false;
    bool m_killScheduled = false;
    bool m_stdoutTruncated = false;
    bool m_stderrTruncated = false;
};
