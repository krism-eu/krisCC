#include "RepositoryExportCore.h"
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>

namespace {
constexpr qint64 kMaximumExportBytes = 128LL * 1024LL * 1024LL;
bool containsControlCharacters(const QString &value)
{
    for (const QChar ch : value)
        if (ch.isNull() || ch.unicode() < 0x20 || ch.unicode() == 0x7f)
            return true;
    return false;
}
bool writeBounded(QSaveFile &file, const QByteArray &data, qint64 *written)
{
    if (!written || *written + data.size() > kMaximumExportBytes)
        return false;
    if (file.write(data) != data.size())
        return false;
    *written += data.size();
    return true;
}
}

QString RepositoryExportCore::repositorySlug(const QString &repository)
{
    if (repository == QStringLiteral("krisCC"))
        return QStringLiteral("krism-eu/krisCC");
    if (repository == QStringLiteral("KrisOS"))
        return QStringLiteral("krism-eu/KrisOS");
    return {};
}

QString RepositoryExportCore::workflowName(const QString &repository)
{
    if (repository == QStringLiteral("krisCC"))
        return QStringLiteral("Build");
    if (repository == QStringLiteral("KrisOS"))
        return QStringLiteral("Build M1");
    return {};
}

QString RepositoryExportCore::safeFileComponent(const QString &value)
{
    QString safe = value.trimmed();
    safe.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("_"));
    while (safe.startsWith(QLatin1Char('.')) || safe.startsWith(QLatin1Char('_')))
        safe.remove(0, 1);
    while (safe.endsWith(QLatin1Char('_')))
        safe.chop(1);
    return safe.isEmpty() ? QStringLiteral("ref") : safe.left(120);
}

RepositoryExportCore::GreenRun RepositoryExportCore::parseLatestGreenRun(
    const QByteArray &json, const QString &repository, QString *error)
{
    if (error) error->clear();
    const QString expectedWorkflow = workflowName(repository);
    if (expectedWorkflow.isEmpty()) {
        if (error) *error = QStringLiteral("Repository non consentito.");
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("Risposta workflow GitHub non valida.");
        return {};
    }
    const QJsonValue runsValue = document.object().value(QStringLiteral("workflow_runs"));
    if (!runsValue.isArray()) {
        if (error) *error = QStringLiteral("Contratto workflow GitHub non riconosciuto.");
        return {};
    }

    static const QRegularExpression shaPattern(QStringLiteral("^[0-9A-Fa-f]{40}$"));
    for (const QJsonValue &value : runsValue.toArray()) {
        if (!value.isObject()) continue;
        const QJsonObject run = value.toObject();
        if (run.value(QStringLiteral("name")).toString() != expectedWorkflow
            || run.value(QStringLiteral("status")).toString() != QStringLiteral("completed")
            || run.value(QStringLiteral("conclusion")).toString() != QStringLiteral("success"))
            continue;
        const QString branch = run.value(QStringLiteral("head_branch")).toString();
        const QString sha = run.value(QStringLiteral("head_sha")).toString();
        if (branch.isEmpty() || branch.size() > 255 || containsControlCharacters(branch)
            || !shaPattern.match(sha).hasMatch()) {
            if (error) *error = QStringLiteral("Ultima build verde contiene un riferimento non valido.");
            return {};
        }
        return {branch, sha.toLower()};
    }

    if (error) *error = QStringLiteral("Nessuna build verde compatibile trovata.");
    return {};
}

bool RepositoryExportCore::writeCombinedRepository(const QString &rootPath,
                                                   const QString &repository,
                                                   const QString &branch,
                                                   const QString &commitSha,
                                                   const QString &destination,
                                                   int *textFiles,
                                                   int *binaryFiles,
                                                   QString *error)
{
    if (textFiles) *textFiles = 0;
    if (binaryFiles) *binaryFiles = 0;
    if (error) error->clear();

    const QFileInfo rootInfo(rootPath);
    const QString canonicalRoot = rootInfo.canonicalFilePath();
    if (canonicalRoot.isEmpty() || !rootInfo.isDir()) {
        if (error) *error = QStringLiteral("Directory repository estratta non valida.");
        return false;
    }

    QStringList files;
    QDirIterator iterator(canonicalRoot,
                          QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext())
        files.append(iterator.next());
    std::sort(files.begin(), files.end());

    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly)
        || !output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        if (error) *error = QStringLiteral("Impossibile creare il file di esportazione.");
        return false;
    }

    qint64 written = 0;
    const QByteArray header =
        QByteArray("# krisCC repository export\n# repository: ") + repository.toUtf8()
        + QByteArray("\n# source: latest successful GitHub Actions build")
        + QByteArray("\n# branch: ") + branch.toUtf8()
        + QByteArray("\n# commit: ") + commitSha.toUtf8()
        + QByteArray("\n# generated: ")
        + QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8()
        + QByteArray("\n# text files embedded verbatim; binary files represented by markers.\n\n");
    if (!writeBounded(output, header, &written)) {
        output.cancelWriting();
        if (error) *error = QStringLiteral("Esportazione troppo grande.");
        return false;
    }

    const QDir root(canonicalRoot);
    for (const QString &absolutePath : files) {
        const QFileInfo info(absolutePath);
        const QString relativePath = root.relativeFilePath(absolutePath);
        if (!writeBounded(output, QByteArray("===== FILE: ") + relativePath.toUtf8()
                          + QByteArray(" =====\n"), &written)) {
            output.cancelWriting();
            if (error) *error = QStringLiteral("Esportazione oltre il limite di 128 MiB.");
            return false;
        }

        if (info.isSymLink()) {
            if (!writeBounded(output, QByteArray("[[SYMLINK -> ")
                              + info.symLinkTarget().toUtf8() + QByteArray("]]\n\n"), &written)) {
                output.cancelWriting();
                if (error) *error = QStringLiteral("Esportazione oltre il limite di 128 MiB.");
                return false;
            }
            if (binaryFiles) ++(*binaryFiles);
            continue;
        }

        QFile input(absolutePath);
        if (!input.open(QIODevice::ReadOnly)) {
            output.cancelWriting();
            if (error) *error = QStringLiteral("Impossibile leggere %1.").arg(relativePath);
            return false;
        }
        if (input.peek(8192).contains('\0')) {
            if (!writeBounded(output, QByteArray("[[BINARY FILE OMITTED · ")
                              + QByteArray::number(info.size()) + QByteArray(" bytes]]\n\n"), &written)) {
                output.cancelWriting();
                if (error) *error = QStringLiteral("Esportazione oltre il limite di 128 MiB.");
                return false;
            }
            if (binaryFiles) ++(*binaryFiles);
            continue;
        }

        while (!input.atEnd()) {
            const QByteArray chunk = input.read(64 * 1024);
            if (chunk.isEmpty() && input.error() != QFileDevice::NoError) {
                output.cancelWriting();
                if (error) *error = QStringLiteral("Errore leggendo %1.").arg(relativePath);
                return false;
            }
            if (!writeBounded(output, chunk, &written)) {
                output.cancelWriting();
                if (error) *error = QStringLiteral("Esportazione oltre il limite di 128 MiB.");
                return false;
            }
        }
        if (!writeBounded(output, QByteArray("\n\n"), &written)) {
            output.cancelWriting();
            if (error) *error = QStringLiteral("Esportazione oltre il limite di 128 MiB.");
            return false;
        }
        if (textFiles) ++(*textFiles);
    }

    if (!output.commit()) {
        if (error) *error = QStringLiteral("Impossibile finalizzare il file di esportazione.");
        return false;
    }
    return true;
}
