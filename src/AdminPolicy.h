#pragma once

#include <QString>
#include <QStringList>

#include <optional>

namespace AdminPolicy {

struct Command {
    QString program;
    QStringList arguments;
    int timeoutMs = 0;
};

std::optional<Command> resolve(const QStringList &request);

}
