#include "MaintenanceTrash.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
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

bool safeTrashSubdirectory(const QString &trashRoot, const QString &name,
                           QString *path, TrashCleanupResult *result,
                           bool allowMissing = true)
{
    const QString candidate = QDir(trashRoot).filePath(name);
    const QFileInfo info(candidate);
    if (!info.exists() && !info.isSymLink()) {
        if (path)
            *path = candidate;
        return allowMissing;
    }
    if (info.isSymLink() || !info.isDir()) {
        result->errors.append(QStringLiteral("Percorso cestino non sicuro ignorato: %1").arg(candidate));
        return false;
    }
    if (isMountPoint(candidate)) {
        result->errors.append(QStringLiteral("Mount nel cestino ignorato per sicurezza: %1").arg(candidate));
        return false;
    }
    if (path)
        *path = candidate;
    return true;
}

bool dataEntryExists(const QString &filesPath, const QString &name)
{
    const QFileInfo info(QDir(filesPath).filePath(name));
    return info.exists() || info.isSymLink();
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

    QString filesPath;
    QString infoPath;
    const bool filesSafe = safeTrashSubdirectory(trashRoot, QStringLiteral("files"),
                                                 &filesPath, &result);
    const bool infoSafe = safeTrashSubdirectory(trashRoot, QStringLiteral("info"),
                                                &infoPath, &result);

    // If either half exists but is unsafe, do not touch the other half. This
    // prevents losing restore metadata while data could still be present behind
    // a rejected symlink/mount/type.
    if (!filesSafe || !infoSafe)
        return result;

    QSet<QString> successfullyRemovedData;
    const QFileInfo filesInfo(filesPath);
    if (filesInfo.exists()) {
        QDir filesDir(filesPath);
        const QFileInfoList dataEntries = filesDir.entryInfoList(
            QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
            QDir::Name);
        for (const QFileInfo &entry : dataEntries) {
            const QString name = entry.fileName();
            if (removeEntry(entry.absoluteFilePath(), &result))
                successfullyRemovedData.insert(name);
        }
    }

    const QFileInfo infoInfo(infoPath);
    if (!infoInfo.exists())
        return result;

    QDir infoDir(infoPath);
    const QFileInfoList metadataEntries = infoDir.entryInfoList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
        QDir::Name);
    for (const QFileInfo &metadata : metadataEntries) {
        const QString fileName = metadata.fileName();
        if (!fileName.endsWith(QStringLiteral(".trashinfo"))) {
            // Unknown metadata is not coupled to a Trash/files name; leave it
            // untouched rather than guessing.
            continue;
        }
        const QString dataName = fileName.left(fileName.size() - qsizetype(10));

        // Metadata may be removed only after the corresponding data entry has
        // gone. This covers both a successful removal in this pass and a true
        // pre-existing orphan, while preserving metadata for every failed data
        // removal.
        if (successfullyRemovedData.contains(dataName)
            || !dataEntryExists(filesPath, dataName)) {
            removeEntry(metadata.absoluteFilePath(), &result);
        }
    }

    return result;
}

}
