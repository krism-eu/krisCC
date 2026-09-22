#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

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

signals:
    void outputReady(const QByteArray &data);
    void finished(ProcessRunner::Outcome outcome, int exitCode,
                  const QByteArray &standardOutput,
                  const QByteArray &standardError,
                  const QString &errorString);

private:
    void drain();
    void finish(Outcome outcome, int exitCode, const QString &errorString = QString());
    void terminateProcess(bool force);
    static void appendBounded(QByteArray &target, const QByteArray &data, qsizetype limit);

    QProcess *m_process = nullptr;
    Options m_options;
    QByteArray m_stdout;
    QByteArray m_stderr;
    bool m_cancelRequested = false;
    bool m_timedOut = false;
    bool m_finished = false;
};
