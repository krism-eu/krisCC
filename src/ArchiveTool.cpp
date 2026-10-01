#include "ArchiveRestoreEngine.h"
#include "BackupSafety.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
int emitResult(const ArchiveOperationResult &result)
{
    QJsonObject object;
    object.insert(QStringLiteral("success"), result.success);
    object.insert(QStringLiteral("partial"), result.partial);
    object.insert(QStringLiteral("applied"), result.applied);
    object.insert(QStringLiteral("members"), result.members);
    object.insert(QStringLiteral("message"), result.message);
    QFile out;
    out.open(stdout, QIODevice::WriteOnly);
    out.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    out.write("\n");
    out.flush();
    return result.success ? 0 : 1;
}

ArchiveOperationResult failure(const QString &message)
{
    ArchiveOperationResult result;
    result.message = message;
    return result;
}

bool prepareWorkspace(const QString &path, QString *sourceStage, QString *error)
{
    const QFileInfo info(path);
    if (!info.isAbsolute() || info.isSymLink() || !info.isDir()) {
        if (error)
            *error = QStringLiteral("Workspace archivio non valido.");
        return false;
    }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                | QFileDevice::ExeOwner);
    qputenv("TMPDIR", QFile::encodeName(path));
    const QString source = QDir(path).filePath(QStringLiteral("source"));
    if (!QDir().mkpath(source)) {
        if (error)
            *error = QStringLiteral("Impossibile creare lo staging della sorgente.");
        return false;
    }
    QFile::setPermissions(source, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                 | QFileDevice::ExeOwner);
    if (sourceStage)
        *sourceStage = source;
    return true;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = QCoreApplication::arguments();
    if (args.size() < 4)
        return emitResult(failure(QStringLiteral("Uso non valido dell'helper archivio.")));

    const QString operation = args.at(1);
    if (operation == QStringLiteral("validate-created")) {
        if (args.size() != 4)
            return emitResult(failure(QStringLiteral("Argomenti validate-created non validi.")));
        QString sourceStage;
        QString workspaceError;
        if (!prepareWorkspace(args.at(3), &sourceStage, &workspaceError))
            return emitResult(failure(workspaceError));
        const QFileInfo info(args.at(2));
        if (info.isSymLink() || !info.isFile())
            return emitResult(failure(QStringLiteral("Il file creato non è un archivio regolare.")));
        return emitResult(ArchiveRestoreEngine::validate(info.absoluteFilePath()));
    }

    if ((operation != QStringLiteral("validate") && operation != QStringLiteral("restore"))
        || (operation == QStringLiteral("validate") && args.size() != 5)
        || (operation == QStringLiteral("restore") && args.size() != 6)) {
        return emitResult(failure(QStringLiteral("Operazione archivio non consentita.")));
    }

    const QString backupRoot = args.at(2);
    const QString requested = args.at(3);
    const QString workspace = args.last();
    QString sourceStage;
    QString workspaceError;
    if (!prepareWorkspace(workspace, &sourceStage, &workspaceError))
        return emitResult(failure(workspaceError));

    QString stablePath;
    QString displayName;
    QString copyError;
    if (!BackupSafety::makeStableArchiveCopy(backupRoot, requested, sourceStage,
                                             &stablePath, &displayName, &copyError)) {
        return emitResult(failure(copyError.isEmpty()
                                      ? QStringLiteral("Impossibile stabilizzare l'archivio sorgente.")
                                      : copyError));
    }

    if (operation == QStringLiteral("validate"))
        return emitResult(ArchiveRestoreEngine::validate(stablePath));

    const QString destinationRoot = args.at(4);
    return emitResult(ArchiveRestoreEngine::restore(stablePath, destinationRoot));
}
