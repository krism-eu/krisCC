#include "RepositoryExportCore.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

#include <algorithm>

namespace {
constexpr qint64 kMaximumExportBytes = 128LL * 1024LL * 1024LL;

bool containsControlCharacters(const QString &value)
{
    for (const QChar ch : value) {
        if (ch.isNull() || ch.unicode() < 0x20 || ch.unicode() == 0x7f)
            return true;
    }
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

QString RepositoryExportCore::safeFileComponent(const QString &value)
{
    QString safe = value.trimmed();
    safe.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("_"));
    while (safe.startsWith(QLatin1Char('.')) || safe.startsWith(QLatin1Char('_')))
        safe.remove(0, 1);
    while (safe.endsWith(QLatin1Char('_')))
        safe.chop(1);
    return safe.isEmpty() ? QStringLiteral("branch") : safe.left(120);
}

QStringList RepositoryExportCore::parseBranchList(const QByteArray &json, QString *error)
{
    if (error)
        error->clear();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        if (error)
            *error = QStringLiteral("Risposta branch GitHub non valida.");
        return {};
    }

    QSet<QString> seen;
    QStringList branches;
    for (const QJsonValue &value : document.array()) {
        if (!value.isObject() || !value.toObject().value(QStringLiteral("name")).isString()) {
            if (error)
                *error = QStringLiteral("Contratto branch GitHub non riconosciuto.");
            return {};
        }
        const QString name = value.toObject().value(QStringLiteral("name")).toString();
        if (name.isEmpty() || name.size() > 255 || containsControlCharacters(name)) {
            if (error)
                *error = QStringLiteral("Nome branch GitHub non valido.");
            return {};
        }
        if (!seen.contains(name)) {
            seen.insert(name);
            branches.append(name);
        }
    }

    std::sort(branches.begin(), branches.end(), [](const QString &a, const QString &b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    const qsizetype mainIndex = branches.indexOf(QStringLiteral("main"));
    if (mainIndex > 0)
        branches.move(mainIndex, 0);
    return branches;
}

bool RepositoryExportCore::writeCombinedRepository(const QString &rootPath,
                                                   const QString &repository,
                                                   const QString &branch,
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
        + QByteArray("\n# branch: ") + branch.toUtf8()
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
        const QByteArray fileHeader = QByteArray("===== FILE: ") + relativePath.toUtf8()
                                    + QByteArray(" =====\n");
        if (!writeBounded(output, fileHeader, &written)) {
            output.cancelWriting();
            if (error) *error = QStringLiteral("Esportazione oltre il limite di 128 MiB.");
            return false;
        }

        if (info.isSymLink()) {
            const QByteArray marker = QByteArray("[[SYMLINK -> ")
                                    + info.symLinkTarget().toUtf8()
                                    + QByteArray("]]\n\n");
            if (!writeBounded(output, marker, &written)) {
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
            const QByteArray marker = QByteArray("[[BINARY FILE OMITTED · ")
                                    + QByteArray::number(info.size())
                                    + QByteArray(" bytes]]\n\n");
            if (!writeBounded(output, marker, &written)) {
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
