#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>

class ProcessRunner;

class CronBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList jobs READ jobs NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool userCronAvailable READ userCronAvailable NOTIFY stateChanged)
    Q_PROPERTY(QString userCronStatus READ userCronStatus NOTIFY stateChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY stateChanged)

public:
    explicit CronBackend(QObject *parent = nullptr);
    ~CronBackend() override;

    const QVariantList &jobs() const { return m_jobs; }
    bool busy() const { return m_busy; }
    bool userCronAvailable() const { return m_userCronAvailable; }
    const QString &userCronStatus() const { return m_userCronStatus; }
    const QString &errorText() const { return m_errorText; }

    Q_INVOKABLE void reload();
    Q_INVOKABLE bool cancel();

signals:
    void stateChanged();

private:
    void loadSystemJobs();
    QVariantList jobsFromText(const QString &text, bool systemFormat,
                              const QString &scope, const QString &source) const;
    void finishUserCron(const QByteArray &standardOutput,
                        const QByteArray &standardError,
                        int exitCode, int outcome);

    QVariantList m_jobs;
    QPointer<ProcessRunner> m_runner;
    bool m_busy = false;
    bool m_userCronAvailable = false;
    QString m_userCronStatus;
    QString m_errorText;
};
