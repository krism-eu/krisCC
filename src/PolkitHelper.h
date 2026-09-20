#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>

class PolkitHelper : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)

public:
    explicit PolkitHelper(QObject *parent = nullptr);

    void execute(const QString &program, const QStringList &args);
    bool running() const { return m_running; }

signals:
    void runningChanged();
    void line(const QString &text);
    void finished(bool success, const QString &output);

private slots:
    void onReadyRead();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessError(QProcess::ProcessError error);

private:
    bool isPrivilegedInvocationAllowed(const QString &program, const QStringList &args) const;
    bool isValidPackageName(const QString &package) const;
    bool isSafeBootToken(const QString &token) const;
    bool isSafeGrubEntry(const QString &entry) const;
    bool isSafeRepositoryId(const QString &repoId) const;
    bool isSafeRepositoryUrl(const QString &url) const;
    int timeoutFor(const QString &program, const QStringList &args) const;
    QString operationLabel() const;
    void consumeOutput(const QByteArray &data, bool flushPartial = false);
    void terminateProcessGroup(bool force);
    void finishWithError(const QString &message);

    bool m_running = false;
    bool m_timedOut = false;
    QProcess *m_process = nullptr;
    QString m_allOutput;
    QByteArray m_lineBuffer;
    QString m_program;
    QStringList m_args;
};
