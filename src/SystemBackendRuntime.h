#pragma once

#include "SystemBackend.h"

#include <QPointer>
#include <QTemporaryDir>

#include <memory>

class SystemdJobCoordinator;

class SystemBackendRuntime final : public SystemBackend
{
    Q_OBJECT
    Q_PROPERTY(bool mutationRunning READ mutationRunning NOTIFY mutationRunningChanged)

public:
    explicit SystemBackendRuntime(PolkitHelper *polkit, QObject *parent = nullptr);
    ~SystemBackendRuntime() override;

    bool mutationRunning() const;
    bool createSnapshot(const QString &kind) override;
    bool cancelSnapshot() override;
    QVariantList backups() const override;
    bool verifySnapshot(const QString &path) override;
    bool restoreSnapshot(const QString &path) override;
    bool deleteSnapshot(const QString &path) override;
    void requestReboot() override;

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
    bool appendBackupDestinationExclusions(const QString &kind, const QString &home,
                                           const QString &backupRoot,
                                           const QString &output,
                                           const QString &partial,
                                           QStringList *arguments,
                                           QString *error) const;
    QString archiveHelperPath() const;
    bool prepareBackupWorkspace(QString *error);
    void clearBackupWorkspace();
    bool startCreatedArchiveValidation(const QString &output, const QString &partial);
    SystemdJobCoordinator *systemdJobs();
    bool startSystemdJob(const QString &service, const QString &action);
    void ensureBootRuntimeConnections();
    void syncBootAggregate();

    std::unique_ptr<QTemporaryDir> m_backupWorkspace;
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
