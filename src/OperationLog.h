#pragma once

#include <QString>

class OperationLog
{
public:
    static void append(const QString &category, const QString &action,
                       const QString &state, const QString &detail = QString());
    static QString recent(int limit = 20);
    static bool clear();

private:
    static QString filePath();
};
