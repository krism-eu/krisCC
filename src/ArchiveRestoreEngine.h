#pragma once

#include <QString>

struct ArchiveOperationResult
{
    bool success = false;
    bool partial = false;
    int applied = 0;
    int members = 0;
    QString message;
};

class ArchiveRestoreEngine
{
public:
    static ArchiveOperationResult validate(const QString &archivePath);
    static ArchiveOperationResult restore(const QString &archivePath,
                                          const QString &destinationRoot);
};
