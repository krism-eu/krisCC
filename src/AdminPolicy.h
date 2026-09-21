#pragma once

#include <QString>
#include <QStringList>

struct AdminCommand
{
    QString program;
    QStringList arguments;
    int timeoutMs = 0;
};

bool buildAdminCommand(const QStringList &applicationArguments, AdminCommand *command);
