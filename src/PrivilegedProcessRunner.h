#pragma once

#include <QString>
#include <QStringList>

int runPrivilegedCommand(const QString &program, const QStringList &arguments, int timeoutMs);
