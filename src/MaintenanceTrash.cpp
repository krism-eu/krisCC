#include "MaintenanceTrash.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>

namespace KrisccMaintenance {
namespace {

bool isMountPoint(const QString &path)
{
    const QStorageInfo storage(path);
    if (!storage.isValid() || !storage.isReady())
        return false;
    return QDir::cleanPath(storage.rootPath()) == QDir::cleanPath(path);
}

bool removeEntry(const QString &path, TrashCleanupResult *result)
{
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink())
        return true;

    if (info.isSymLink()) {
        if (QFile::remove(path)) {
            ++result->entriesRemoved;
            return true;
        }
        result->errors.append(QStringLiteral("Impossibile rimuovere il collegamento: %1").arg(path));
        return false;
    }

    if (!info.isDir()) {
        const quint64 size = info.isFile() ? quint64(info.size()) : 0;
        if (QFile::remove(path)) {
            ++result->entriesRemoved;
            result->bytesRemoved += size;
            return true;
        }
        result->errors.append(QStringLiteral("Impossibile rimuovere: %1").arg(path));
        return false;
    }

    if (isMountPoint(path)) {
        result->errors.append(QStringLiteral("Mount annidato ignorato per sicurezza: %1").arg(path));
        return false;
    }

    QDir directory(path);
    const QFileInfoList entries = directory.entryInfoList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
        QDir::Name);
    bool ok = true;
    for (const QFileInfo &entry : entries)
        ok = removeEntry(entry.absoluteFilePath(), result) && ok;

    if (!ok)
        return false;

    QDir parent = info.dir();
    if (parent.rmdir(info.fileName())) {
        ++result->entriesRemoved;
        return true;
    }

    result->errors.append(QStringLiteral("Impossibile rimuovere la directory: %1").arg(path));
    return false;
}

void cleanSubdirectory(const QString &trashRoot, const QString &name, TrashCleanupResult *result)
{
    const QString path = QDir(trashRoot).filePath(name);
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink())
        return;

    if (info.isSymLink() || !info.isDir()) {
        result->errors.append(QStringLiteral("Percorso cestino non sicuro ignorato: %1").arg(path));
        return;
    }

    if (isMountPoint(path)) {
        result->errors.append(QStringLiteral("Mount nel cestino ignorato per sicurezza: %1").arg(path));
        return;
    }

    QDir directory(path);
    const QFileInfoList entries = directory.entryInfoList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
        QDir::Name);
    for (const QFileInfo &entry : entries)
        removeEntry(entry.absoluteFilePath(), result);
}

}

void TrashCleanupResult::merge(const TrashCleanupResult &other)
{
    entriesRemoved += other.entriesRemoved;
    bytesRemoved += other.bytesRemoved;
    errors.append(other.errors);
}

TrashCleanupResult cleanTrashRoot(const QString &trashRoot)
{
    TrashCleanupResult result;
    const QFileInfo rootInfo(trashRoot);
    if (!rootInfo.exists() && !rootInfo.isSymLink())
        return result;

    if (rootInfo.isSymLink() || !rootInfo.isDir() || isMountPoint(trashRoot)) {
        result.errors.append(QStringLiteral("Radice cestino non sicura ignorata: %1").arg(trashRoot));
        return result;
    }

    cleanSubdirectory(trashRoot, QStringLiteral("files"), &result);
    cleanSubdirectory(trashRoot, QStringLiteral("info"), &result);
    return result;
}

}
