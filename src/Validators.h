#pragma once

#include <QString>

namespace Validators {bool packageName(const QString &value);
bool repositoryId(const QString &value);
bool repositoryUrl(const QString &value);
bool bootToken(const QString &value);
bool grubEntry(const QString &value);
bool archiveMemberPath(const QString &value);
bool archiveVerboseEntry(const QString &line);
}
