#include "Validators.h"

#include <QDir>
#include <QRegularExpression>
#include <QUrl>

namespace Validators {

bool packageName(const QString &value)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9+_.-]{0,127}$"));
    if (!pattern.match(value).hasMatch())
        return false;
    return !value.endsWith(QStringLiteral(".rpm"))
        && !value.endsWith(QStringLiteral(".i686"))
        && !value.endsWith(QStringLiteral(".x86_64"))
        && !value.endsWith(QStringLiteral(".noarch"));
}

bool repositoryId(const QString &value)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$"));
    return pattern.match(value).hasMatch();
}

bool repositoryUrl(const QString &value)
{
    if (value.isEmpty() || value.size() > 2048
        || value.contains(QRegularExpression(QStringLiteral("[\\s\\x00-\\x1f]"))))
        return false;
    const QUrl url(value);
    return url.isValid()
        && url.scheme() == QStringLiteral("https")
        && !url.host().isEmpty()
        && url.userInfo().isEmpty();
}

bool bootToken(const QString &value)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9A-Fa-f]{4}$"));
    return pattern.match(value).hasMatch();
}

bool archiveMemberPath(const QString &value)
{
    if (value.isEmpty() || value.startsWith(QLatin1Char('/')) || value.contains(QLatin1Char('\0')))
        return false;
    const QString normalized = QDir::cleanPath(value);
    if (normalized == QStringLiteral("..") || normalized.startsWith(QStringLiteral("../")))
        return false;
    const QStringList parts = value.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    return !parts.contains(QStringLiteral(".."));
}

bool archiveVerboseEntry(const QString &line)
{
    if (line.isEmpty())
        return false;
    const QChar type = line.at(0);
    if (type != QLatin1Char('-') && type != QLatin1Char('d') && type != QLatin1Char('l'))
        return false;
    if (type == QLatin1Char('l')) {
        const qsizetype arrow = line.indexOf(QStringLiteral(" -> "));
        if (arrow < 0)
            return false;
        const QString target = line.mid(arrow + 4).trimmed();
        if (!archiveMemberPath(target))
            return false;
    }
    return true;
}

bool grubEntry(const QString &value)
{
    if (value.isEmpty() || value.size() > 256 || value.startsWith(QLatin1Char('-')))
        return false;
    for (const QChar ch : value) {
        if (ch.isNull() || ch.unicode() < 0x20 || ch.unicode() == 0x7f)
            return false;
    }
    return true;
}


}
