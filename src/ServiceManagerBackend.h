#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>

class ProcessRunner;

class ServiceManagerBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList activeServices READ activeServices NOTIFY stateChanged)
    Q_PROPERTY(QVariantList services READ services NOTIFY stateChanged)
    Q_PROPERTY(QVariantList failedUnits READ failedUnits NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString message READ message NOTIFY stateChanged)
    Q_PROPERTY(QString journalText READ journalText NOTIFY stateChanged)
    Q_PROPERTY(bool userScope READ userScope NOTIFY stateChanged)

public:
    enum class Task {
        None,
        ActiveServices,
        ServicesUnits,
        ServicesFiles,
        FailedUnits,
        Journal,
        Control
    };

    explicit ServiceManagerBackend(QObject *parent = nullptr);
    ~ServiceManagerBackend() override;

    const QVariantList &activeServices() const { return m_activeServices; }
    const QVariantList &services() const { return m_services; }
    const QVariantList &failedUnits() const { return m_failedUnits; }
    bool busy() const { return m_busy; }
    const QString &state() const { return m_state; }
    const QString &message() const { return m_message; }
    const QString &journalText() const { return m_journalText; }
    bool userScope() const { return m_userScope; }

    Q_INVOKABLE bool refreshActiveServices(bool userScope);
    Q_INVOKABLE bool refreshServices(bool userScope);
    Q_INVOKABLE bool refreshFailedUnits(bool userScope);
    Q_INVOKABLE bool loadJournal(const QString &unit, bool userScope,
                                 const QString &priority = QStringLiteral("warning"));
    Q_INVOKABLE bool controlUnit(const QString &unit, bool userScope, const QString &action);
    Q_INVOKABLE bool cancel();

signals:
    void stateChanged();
    void controlFinished(bool userScope, const QString &unit,
                         const QString &action, bool success);

private:
    friend class ServiceManagerReentrancyTest;

    bool startProcess(const QStringList &arguments, Task task, int timeoutMs = 15000);
    void handleFinished(Task task, int exitCode, int outcome,
                        const QByteArray &stdoutData, const QByteArray &stderrData,
                        const QString &errorString, bool outputTruncated);
    bool validUnit(const QString &unit) const;
    bool validJournalUnit(const QString &unit) const;
    void finish(const QString &state, const QString &message = QString());
    void startUnitFilesQuery();

    QVariantList m_activeServices;
    QVariantList m_services;
    QVariantList m_failedUnits;
    QVariantList m_pendingServices;
    QPointer<ProcessRunner> m_runner;
    bool m_busy = false;
    bool m_userScope = false;
    QString m_state = QStringLiteral("idle");
    QString m_message;
    QString m_journalText;
    QString m_controlAction;
    QString m_controlUnit;
};
