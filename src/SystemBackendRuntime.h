#pragma once

#include "SystemBackend.h"

class SystemBackendRuntime final : public SystemBackend
{
public:
    explicit SystemBackendRuntime(PolkitHelper *polkit, QObject *parent = nullptr)
        : SystemBackend(polkit, parent) {}

    bool createSnapshot(const QString &kind) override;
    bool cancelSnapshot() override;
    QVariantList backups() const override;
    bool deleteSnapshot(const QString &path) override;

private:
    bool appendBackupDestinationExclusions(const QString &kind, const QString &home,
                                           const QString &backupRoot,
                                           const QString &output,
                                           const QString &partial,
                                           QStringList *arguments,
                                           QString *error) const;
};
