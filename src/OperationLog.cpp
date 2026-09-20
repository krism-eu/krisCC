#include "OperationLog.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextStream>

namespace {
constexpr qint64 kMaxLogBytes = 512 * 1024;
constexpr qint64 kKeepLogBytes = 256 * 1024;

void trimLogIfNeeded(const QString &path)
{
    QFile source(path);
    if (!source.open(QIODevice::ReadOnly) || source.size() <= kMaxLogBytes)
        return;

    const qint64 start = qMax<qint64>(0, source.size() - kKeepLogBytes);
    if (!source.seek(start))
        return;

    QByteArray tail = source.readAll();
    if (start > 0) {
        const qsizetype newline = tail.indexOf('\n');
        if (newline >= 0)
            tail.remove(0, newline + 1);
    }
    source.close();

    QSaveFile destination(path);
    if (!destination.open(QIODevice::WriteOnly)
        || !destination.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        || destination.write(tail) != tail.size())
        return;
    destination.commit();
}
}

QString OperationLog::filePath()
{
    return QDir::homePath() + QStringLiteral("/.local/state/krisCC/history.jsonl");
}

void OperationLog::append(const QString &category, const QString &action,
                          const QString &state, const QString &detail)
{
    const QString path = filePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    trimLogIfNeeded(path);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    QJsonObject object;
    object.insert(QStringLiteral("time"), QDateTime::currentDateTime().toString(Qt::ISODate));
    object.insert(QStringLiteral("category"), category.left(64));
    object.insert(QStringLiteral("action"), action.left(160));
    object.insert(QStringLiteral("state"), state.left(32));
    if (!detail.trimmed().isEmpty())
        object.insert(QStringLiteral("detail"), detail.simplified().left(240));

    file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    file.write("\n");
}

QString OperationLog::recent(int limit)
{
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    QList<QByteArray> lines;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (!line.isEmpty())
            lines.append(line);
    }

    QString result;
    QTextStream out(&result);
    int emitted = 0;
    for (qsizetype i = lines.size(); i > 0 && emitted < qMax(1, limit); --i) {
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(lines.at(i - 1), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject())
            continue;
        const QJsonObject object = document.object();
        out << object.value(QStringLiteral("time")).toString()
            << QStringLiteral(" · ")
            << object.value(QStringLiteral("category")).toString()
            << QStringLiteral(" · ")
            << object.value(QStringLiteral("action")).toString()
            << QStringLiteral(" · ")
            << object.value(QStringLiteral("state")).toString();
        const QString detail = object.value(QStringLiteral("detail")).toString();
        if (!detail.isEmpty())
            out << QStringLiteral("\n  ") << detail;
        out << '\n';
        ++emitted;
    }
    return result.trimmed();
}

bool OperationLog::clear()
{
    const QString path = filePath();
    return !QFileInfo::exists(path) || QFile::remove(path);
}
