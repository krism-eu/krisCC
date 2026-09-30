#pragma once

#include <QList>
#include <QString>

namespace CronParser
{
struct Entry {
    QString schedule;
    QString command;
    QString user;
};

QList<Entry> parse(const QString &text, bool systemFormat);
QString describe(const QString &schedule);
}
