#include "CronParser.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QStringList>

namespace {
bool isEnvironmentLine(const QString &line)
{
    static const QRegularExpression expression(
        QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*\\s*="));
    return expression.match(line).hasMatch();
}

bool parseNumber(const QString &value, int minimum, int maximum, int *result)
{
    bool ok = false;
    const int number = value.toInt(&ok);
    if (!ok || number < minimum || number > maximum)
        return false;
    if (result)
        *result = number;
    return true;
}

QString translated(const char *text)
{
    return QCoreApplication::translate("CronParser", text);
}

QString weekdayName(int value)
{
    switch (value) {
    case 0:
    case 7:
        return translated("domenica");
    case 1:
        return translated("lunedì");
    case 2:
        return translated("martedì");
    case 3:
        return translated("mercoledì");
    case 4:
        return translated("giovedì");
    case 5:
        return translated("venerdì");
    case 6:
        return translated("sabato");
    default:
        return QString();
    }
}
}

QList<CronParser::Entry> CronParser::parse(const QString &text, bool systemFormat)
{
    QList<Entry> entries;

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (QString line : lines) {
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);

        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#'))
            || isEnvironmentLine(trimmed)) {
            continue;
        }

        QRegularExpressionMatch match;
        if (trimmed.startsWith(QLatin1Char('@'))) {
            static const QRegularExpression userMacro(
                QStringLiteral("^(@\\S+)\\s+(.+)$"));
            static const QRegularExpression systemMacro(
                QStringLiteral("^(@\\S+)\\s+(\\S+)\\s+(.+)$"));

            match = (systemFormat ? systemMacro : userMacro).match(trimmed);
            if (!match.hasMatch())
                continue;

            Entry entry;
            entry.schedule = match.captured(1);
            if (systemFormat) {
                entry.user = match.captured(2);
                entry.command = match.captured(3).trimmed();
            } else {
                entry.command = match.captured(2).trimmed();
            }
            if (!entry.command.isEmpty())
                entries.append(entry);
            continue;
        }

        static const QRegularExpression userEntry(
            QStringLiteral("^(\\S+)\\s+(\\S+)\\s+(\\S+)\\s+(\\S+)\\s+(\\S+)\\s+(.+)$"));
        static const QRegularExpression systemEntry(
            QStringLiteral("^(\\S+)\\s+(\\S+)\\s+(\\S+)\\s+(\\S+)\\s+(\\S+)\\s+(\\S+)\\s+(.+)$"));

        match = (systemFormat ? systemEntry : userEntry).match(trimmed);
        if (!match.hasMatch())
            continue;

        Entry entry;
        entry.schedule = QStringLiteral("%1 %2 %3 %4 %5")
                             .arg(match.captured(1), match.captured(2),
                                  match.captured(3), match.captured(4),
                                  match.captured(5));
        if (systemFormat) {
            entry.user = match.captured(6);
            entry.command = match.captured(7).trimmed();
        } else {
            entry.command = match.captured(6).trimmed();
        }
        if (!entry.command.isEmpty())
            entries.append(entry);
    }

    return entries;
}

QString CronParser::describe(const QString &schedule)
{
    const QString value = schedule.trimmed();

    if (value == QStringLiteral("@reboot"))
        return translated("All'avvio");
    if (value == QStringLiteral("@hourly"))
        return translated("Ogni ora");
    if (value == QStringLiteral("@daily") || value == QStringLiteral("@midnight"))
        return translated("Ogni giorno");
    if (value == QStringLiteral("@weekly"))
        return translated("Ogni settimana");
    if (value == QStringLiteral("@monthly"))
        return translated("Ogni mese");
    if (value == QStringLiteral("@yearly") || value == QStringLiteral("@annually"))
        return translated("Ogni anno");

    const QStringList parts = value.split(QRegularExpression(QStringLiteral("\\s+")),
                                          Qt::SkipEmptyParts);
    if (parts.size() != 5)
        return value;

    const QString &minute = parts.at(0);
    const QString &hour = parts.at(1);
    const QString &dayOfMonth = parts.at(2);
    const QString &month = parts.at(3);
    const QString &dayOfWeek = parts.at(4);

    if (minute == QStringLiteral("*") && hour == QStringLiteral("*")
        && dayOfMonth == QStringLiteral("*") && month == QStringLiteral("*")
        && dayOfWeek == QStringLiteral("*")) {
        return translated("Ogni minuto");
    }

    static const QRegularExpression intervalExpression(QStringLiteral("^\\*/([0-9]+)$"));
    const QRegularExpressionMatch intervalMatch = intervalExpression.match(minute);
    int interval = 0;
    if (intervalMatch.hasMatch()
        && parseNumber(intervalMatch.captured(1), 1, 59, &interval)
        && hour == QStringLiteral("*") && dayOfMonth == QStringLiteral("*")
        && month == QStringLiteral("*") && dayOfWeek == QStringLiteral("*")) {
        return translated("Ogni %1 minuti").arg(interval);
    }

    int minuteNumber = 0;
    int hourNumber = 0;
    if (parseNumber(minute, 0, 59, &minuteNumber)
        && hour == QStringLiteral("*")
        && dayOfMonth == QStringLiteral("*") && month == QStringLiteral("*")
        && dayOfWeek == QStringLiteral("*")) {
        return translated("Ogni ora al minuto %1").arg(minuteNumber);
    }

    if (parseNumber(minute, 0, 59, &minuteNumber)
        && parseNumber(hour, 0, 23, &hourNumber)
        && dayOfMonth == QStringLiteral("*") && month == QStringLiteral("*")
        && dayOfWeek == QStringLiteral("*")) {
        return translated("Ogni giorno alle %1:%2")
            .arg(hourNumber, 2, 10, QLatin1Char('0'))
            .arg(minuteNumber, 2, 10, QLatin1Char('0'));
    }

    int weekday = 0;
    if (parseNumber(minute, 0, 59, &minuteNumber)
        && parseNumber(hour, 0, 23, &hourNumber)
        && dayOfMonth == QStringLiteral("*") && month == QStringLiteral("*")
        && parseNumber(dayOfWeek, 0, 7, &weekday)) {
        return translated("Ogni %1 alle %2:%3")
            .arg(weekdayName(weekday))
            .arg(hourNumber, 2, 10, QLatin1Char('0'))
            .arg(minuteNumber, 2, 10, QLatin1Char('0'));
    }

    int monthDay = 0;
    if (parseNumber(minute, 0, 59, &minuteNumber)
        && parseNumber(hour, 0, 23, &hourNumber)
        && parseNumber(dayOfMonth, 1, 31, &monthDay)
        && month == QStringLiteral("*") && dayOfWeek == QStringLiteral("*")) {
        return translated("Ogni mese il giorno %1 alle %2:%3")
            .arg(monthDay)
            .arg(hourNumber, 2, 10, QLatin1Char('0'))
            .arg(minuteNumber, 2, 10, QLatin1Char('0'));
    }

    return value;
}
