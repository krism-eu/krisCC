#include "MaintenanceTrash.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QStorageInfo>
#include <QTextStream>

#include <sys/types.h>
#include <unistd.h>

namespace {

bool isSafeComponent(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists())
        return true;
    if (info.isSymLink())
        return false;

    const QStorageInfo storage(path);
    if (storage.isValid() && storage.isReady()
        && QDir::cleanPath(storage.rootPath()) == QDir::cleanPath(path))
        return false;
    return true;
}

bool safeHomeTrash(const QString &home, QString *trashRoot)
{
    const QFileInfo homeInfo(home);
    const QString canonicalHome = homeInfo.canonicalFilePath();
    if (canonicalHome.isEmpty() || !homeInfo.isDir() || homeInfo.isSymLink())
        return false;

    QString current = canonicalHome;
    for (const QString &part : {QStringLiteral(".local"), QStringLiteral("share"),
                                QStringLiteral("Trash")}) {
        current = QDir(current).filePath(part);
        if (!isSafeComponent(current))
            return false;
    }

    const QFileInfo trashInfo(current);
    if (trashInfo.exists()) {
        const QString canonicalTrash = trashInfo.canonicalFilePath();
        if (canonicalTrash.isEmpty()
            || !canonicalTrash.startsWith(canonicalHome + QLatin1Char('/')))
            return false;
    }

    *trashRoot = current;
    return true;
}

KrisccMaintenance::TrashCleanupResult cleanHome()
{
    KrisccMaintenance::TrashCleanupResult result;
    const QString home = QDir::homePath();
    QString trashRoot;
    if (home.isEmpty() || !safeHomeTrash(home, &trashRoot)) {
        result.errors.append(QStringLiteral("Home o cestino utente non sicuro; pulizia annullata."));
        return result;
    }

    result.merge(KrisccMaintenance::cleanTrashRoot(trashRoot));
    return result;
}

KrisccMaintenance::TrashCleanupResult cleanSystem(uid_t uid)
{
    KrisccMaintenance::TrashCleanupResult result;
    const QString uidText = QString::number(qulonglong(uid));
    QSet<QString> roots;

    for (const QStorageInfo &storage : QStorageInfo::mountedVolumes()) {
        if (!storage.isValid() || !storage.isReady() || storage.isReadOnly())
            continue;
        if (!storage.device().startsWith("/dev/"))
            continue;

        const QString root = QDir::cleanPath(storage.rootPath());
        if (!root.isEmpty())
            roots.insert(root);
    }

    for (const QString &root : roots) {
        const QString sharedBase = QDir(root).filePath(QStringLiteral(".Trash"));
        const QFileInfo sharedInfo(sharedBase);
        if (sharedInfo.exists() && !sharedInfo.isSymLink() && sharedInfo.isDir()) {
            result.merge(KrisccMaintenance::cleanTrashRoot(
                QDir(sharedBase).filePath(uidText)));
        } else if (sharedInfo.isSymLink()) {
            result.errors.append(
                QStringLiteral("Cestino condiviso symlink ignorato: %1").arg(sharedBase));
        }

        result.merge(KrisccMaintenance::cleanTrashRoot(
            QDir(root).filePath(QStringLiteral(".Trash-") + uidText)));
    }

    return result;
}

QString humanSize(quint64 bytes)
{
    if (bytes >= 1024ULL * 1024ULL * 1024ULL)
        return QString::number(double(bytes) / (1024.0 * 1024.0 * 1024.0), 'f', 1)
             + QStringLiteral(" GiB");
    if (bytes >= 1024ULL * 1024ULL)
        return QString::number(double(bytes) / (1024.0 * 1024.0), 'f', 1)
             + QStringLiteral(" MiB");
    if (bytes >= 1024ULL)
        return QString::number(double(bytes) / 1024.0, 'f', 1) + QStringLiteral(" KiB");
    return QString::number(bytes) + QStringLiteral(" B");
}

}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);

    if (::geteuid() == 0) {
        err << "kriscc-maintenance: esecuzione come root rifiutata.\n";
        return 77;
    }

    const QStringList arguments = QCoreApplication::arguments();
    if (arguments.size() != 2) {
        err << "kriscc-maintenance: modalità non valida.\n";
        return 64;
    }

    const QString mode = arguments.at(1);
    if (mode != QStringLiteral("trash-home")
        && mode != QStringLiteral("trash-system")
        && mode != QStringLiteral("trash-all")) {
        err << "kriscc-maintenance: modalità non consentita.\n";
        return 64;
    }

    KrisccMaintenance::TrashCleanupResult result;
    if (mode == QStringLiteral("trash-home") || mode == QStringLiteral("trash-all"))
        result.merge(cleanHome());
    if (mode == QStringLiteral("trash-system") || mode == QStringLiteral("trash-all"))
        result.merge(cleanSystem(::geteuid()));

    out << "Pulizia cestini completata: " << result.entriesRemoved
        << " elementi rimossi, " << humanSize(result.bytesRemoved)
        << " di dati file elaborati.\n";

    if (!result.errors.isEmpty()) {
        err << "Alcuni percorsi sono stati ignorati o non rimossi:\n";
        for (const QString &message : result.errors)
            err << "- " << message << '\n';
        return 1;
    }

    return 0;
}
