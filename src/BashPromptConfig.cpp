#include "BashPromptConfig.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringConverter>

namespace {
const QString kStartMarker = QStringLiteral("# >>> krisCC prompt >>>");
const QString kEndMarker = QStringLiteral("# <<< krisCC prompt <<<");

bool decodeUtf8(const QByteArray &bytes, QString *text)
{
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString decoded = decoder.decode(bytes);
    if (decoder.hasError())
        return false;
    if (text)
        *text = decoded;
    return true;
}

bool readExisting(const QString &path, QString *text, QFileDevice::Permissions *permissions,
                  QString *error)
{
    const QFileInfo info(path);

    if (info.isSymLink()) {
        if (error)
            *error = QStringLiteral("~/.bashrc è un collegamento simbolico.");
        return false;
    }

    if (!info.exists()) {
        if (text)
            text->clear();
        if (permissions)
            *permissions = QFileDevice::ReadOwner | QFileDevice::WriteOwner
                         | QFileDevice::ReadGroup | QFileDevice::ReadOther;
        return true;
    }

    if (!info.isFile() || !info.isReadable()) {
        if (error)
            *error = QStringLiteral("~/.bashrc non è un file regolare leggibile.");
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Impossibile leggere ~/.bashrc.");
        return false;
    }

    QString decoded;
    if (!decodeUtf8(file.readAll(), &decoded)) {
        if (error)
            *error = QStringLiteral("~/.bashrc non è testo UTF-8 valido.");
        return false;
    }

    if (text)
        *text = decoded;
    if (permissions)
        *permissions = file.permissions();
    return true;
}

bool markerRange(const QString &text, qsizetype *start, qsizetype *end, QString *error)
{
    const qsizetype starts = text.count(kStartMarker);
    const qsizetype ends = text.count(kEndMarker);

    if (starts == 0 && ends == 0) {
        if (start)
            *start = -1;
        if (end)
            *end = -1;
        return true;
    }

    if (starts != 1 || ends != 1) {
        if (error)
            *error = QStringLiteral("I marker krisCC in ~/.bashrc sono mancanti o duplicati.");
        return false;
    }

    const qsizetype begin = text.indexOf(kStartMarker);
    const qsizetype markerEnd = text.indexOf(kEndMarker);
    if (begin < 0 || markerEnd < begin) {
        if (error)
            *error = QStringLiteral("I marker krisCC in ~/.bashrc non sono ordinati correttamente.");
        return false;
    }

    if (start)
        *start = begin;
    if (end)
        *end = markerEnd + kEndMarker.size();
    return true;
}

QString presetBlock(const QString &presetId)
{
    QString ps1;

    if (presetId == QStringLiteral("readable")) {
        ps1 = QStringLiteral(
            "PS1='\\n\\[\\e[1;36m\\]\\u@\\h\\[\\e[0m\\] "
            "\\[\\e[1;34m\\]\\w\\[\\e[0m\\]\\n\\$ '");
    } else if (presetId == QStringLiteral("compact")) {
        ps1 = QStringLiteral(
            "PS1='\\[\\e[1;36m\\]\\u@\\h\\[\\e[0m\\]:"
            "\\[\\e[1;34m\\]\\w\\[\\e[0m\\]\\$ '");
    } else if (presetId == QStringLiteral("minimal")) {
        ps1 = QStringLiteral(
            "PS1='\\n\\[\\e[1;34m\\]\\w\\[\\e[0m\\]\\n\\$ '");
    } else {
        return {};
    }

    return kStartMarker
        + QStringLiteral("\n# Gestito da krisCC. Modifica/rimozione tramite il Control Center.\n"
                         "if [[ $- == *i* ]]; then\n    ")
        + ps1
        + QStringLiteral("\nfi\n")
        + kEndMarker;
}

bool writeAtomically(const QString &path, const QString &text,
                     QFileDevice::Permissions permissions, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("Impossibile aprire ~/.bashrc per la scrittura atomica.");
        return false;
    }

    if (!file.setPermissions(permissions)) {
        file.cancelWriting();
        if (error)
            *error = QStringLiteral("Impossibile preservare i permessi di ~/.bashrc.");
        return false;
    }

    const QByteArray bytes = text.toUtf8();
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (error)
            *error = QStringLiteral("Scrittura atomica di ~/.bashrc non riuscita.");
        return false;
    }

    return true;
}
} // namespace

namespace BashPromptConfig {

Status inspect(const QString &path)
{
    const QFileInfo info(path);
    if (info.isSymLink())
        return Status::Symlink;
    if (!info.exists())
        return Status::Missing;
    if (!info.isFile() || !info.isReadable())
        return Status::Error;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return Status::Error;

    QString text;
    if (!decodeUtf8(file.readAll(), &text))
        return Status::Error;

    qsizetype start = -1;
    qsizetype end = -1;
    if (!markerRange(text, &start, &end, nullptr))
        return Status::InvalidMarkers;

    return start >= 0 ? Status::Managed : Status::Unmanaged;
}

QString statusId(Status status)
{
    switch (status) {
    case Status::Missing:
        return QStringLiteral("missing");
    case Status::Unmanaged:
        return QStringLiteral("unmanaged");
    case Status::Managed:
        return QStringLiteral("managed");
    case Status::InvalidMarkers:
        return QStringLiteral("invalid");
    case Status::Symlink:
        return QStringLiteral("symlink");
    case Status::Error:
        return QStringLiteral("error");
    }
    return QStringLiteral("error");
}

bool applyPreset(const QString &path, const QString &presetId, QString *error)
{
    const QString block = presetBlock(presetId);
    if (block.isEmpty()) {
        if (error)
            *error = QStringLiteral("Preset del prompt non riconosciuto.");
        return false;
    }

    QString text;
    QFileDevice::Permissions permissions;
    if (!readExisting(path, &text, &permissions, error))
        return false;

    qsizetype start = -1;
    qsizetype end = -1;
    if (!markerRange(text, &start, &end, error))
        return false;

    QString updated;
    if (start >= 0) {
        updated = text;
        updated.replace(start, end - start, block);
    } else {
        updated = text;
        if (!updated.isEmpty() && !updated.endsWith(QLatin1Char('\n')))
            updated += QLatin1Char('\n');
        updated += block;
        updated += QLatin1Char('\n');
    }

    return writeAtomically(path, updated, permissions, error);
}

bool removeManagedBlock(const QString &path, QString *error)
{
    const QFileInfo info(path);
    if (info.isSymLink()) {
        if (error)
            *error = QStringLiteral("~/.bashrc è un collegamento simbolico.");
        return false;
    }

    if (!info.exists())
        return true;

    QString text;
    QFileDevice::Permissions permissions;
    if (!readExisting(path, &text, &permissions, error))
        return false;

    qsizetype start = -1;
    qsizetype end = -1;
    if (!markerRange(text, &start, &end, error))
        return false;

    if (start < 0)
        return true;

    qsizetype removeEnd = end;
    if (removeEnd < text.size() && text.at(removeEnd) == QLatin1Char('\n'))
        ++removeEnd;

    QString updated = text;
    updated.remove(start, removeEnd - start);

    return writeAtomically(path, updated, permissions, error);
}

} // namespace BashPromptConfig
