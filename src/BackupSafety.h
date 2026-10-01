#pragma once

#include <QString>
#include <QVariantList>

namespace BackupSafety {

bool validBackupName(const QString &name);
QString backupKind(const QString &name);
QVariantList listBackups(const QString &backupRoot, QString *error = nullptr);
bool removeBackup(const QString &backupRoot, const QString &requestedPath,
                  QString *removedName = nullptr, QString *error = nullptr);

// Copies one validated regular archive through an already opened descriptor into
// a private staging directory. The source is checked again after the copy; a
// concurrent in-place modification makes the operation fail rather than
// validating/extracting a mixed snapshot.
bool makeStableArchiveCopy(const QString &backupRoot, const QString &requestedPath,
                           const QString &stagingDirectory, QString *stablePath,
                           QString *displayName = nullptr, QString *error = nullptr);

} // namespace BackupSafety
