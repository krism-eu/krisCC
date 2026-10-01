#pragma once

#include "SystemBackend.h"

#include <QTemporaryDir>

#include <memory>

class SystemBackendRuntime final : public SystemBackend
{
public:
    explicit SystemBackendRuntime(PolkitHelper *polkit, QObject *parent = nullptr)
        : SystemBackend(polkit, parent) {}
    ~SystemBackendRuntime() override;

    bool createSnapshot(const QString &kind) override;
    bool cancelSnapshot() override;
    QVariantList backups() const override;
    bool verifySnapshot(const QString &path) override;
    bool restoreSnapshot(const QString &path) override;
    bool deleteSnapshot(const QString &path) override;
    void requestReboot() override;

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

    std::unique_ptr<QTemporaryDir> m_backupWorkspace;
};
