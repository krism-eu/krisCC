#pragma once

#include <QString>

namespace BashPromptConfig {

enum class Status {
    Missing,
    Unmanaged,
    Managed,
    InvalidMarkers,
    Symlink,
    Error
};

Status inspect(const QString &path);
QString statusId(Status status);
bool applyPreset(const QString &path, const QString &presetId, QString *error);
bool removeManagedBlock(const QString &path, QString *error);

} // namespace BashPromptConfig
