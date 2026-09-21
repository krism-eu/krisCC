#include "Validators.h"

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

bool containerName(const QString &value)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]{0,127}$"));
    return pattern.match(value).hasMatch();
}

bool flatpakId(const QString &value)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,255}$"));
    return pattern.match(value).hasMatch();
}

}
