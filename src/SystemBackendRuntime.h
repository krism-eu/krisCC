#pragma once

#include "SystemBackend.h"

#include <QPointer>


class SystemdJobCoordinator;

class SystemBackendRuntime final : public SystemBackend
{
    Q_OBJECT
    Q_PROPERTY(bool mutationRunning READ mutationRunning NOTIFY mutationRunningChanged)
    Q_PROPERTY(bool uefiEntriesBusy READ uefiEntriesBusy NOTIFY bootEntriesChanged)
    Q_PROPERTY(bool grubEntriesBusy READ grubEntriesBusy NOTIFY bootEntriesChanged)
    Q_PROPERTY(QString uefiEntriesError READ uefiEntriesError NOTIFY bootEntriesChanged)
    Q_PROPERTY(QString grubEntriesError READ grubEntriesError NOTIFY bootEntriesChanged)

public:
    explicit SystemBackendRuntime(PolkitHelper *polkit, QObject *parent = nullptr);
    ~SystemBackendRuntime() override;

    bool mutationRunning() const;
    bool uefiEntriesBusy() const { return m_uefiBusy || m_runtimeBootReadOwned; }
    bool grubEntriesBusy() const { return m_grubBusy; }
    const QString &uefiEntriesError() const { return m_uefiError; }
    const QString &grubEntriesError() const { return m_grubError; }

    void requestReboot() override;
    Q_INVOKABLE void requestFirmwareReboot() override;

    void refreshServiceStates() override;
    bool startService(const QString &service) override;
    bool stopService(const QString &service) override;
    bool restartService(const QString &service) override;

    void refreshUefiEntries() override;
    void refreshUefiEntriesPrivileged() override;
    void refreshGrubEntries() override;

signals:
    void mutationRunningChanged();

private:
    SystemdJobCoordinator *systemdJobs();
    bool startSystemdJob(const QString &service, const QString &action);
    void ensureBootRuntimeConnections();
    void syncBootAggregate();

    SystemdJobCoordinator *m_systemdJobCoordinator = nullptr;

    QPointer<QProcess> m_uefiProcess;
    QPointer<QProcess> m_grubProcess;
    bool m_uefiBusy = false;
    bool m_grubBusy = false;
    bool m_runtimeBootReadOwned = false;
    bool m_bootRuntimeConnectionsInitialized = false;
    QString m_uefiError;
    QString m_grubError;
    quint64 m_uefiRequestGeneration = 0;
    quint64 m_grubRequestGeneration = 0;
};
