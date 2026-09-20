#pragma once

#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace KrisccMaintenance {

struct TrashCleanupResult
{
    int entriesRemoved = 0;
    quint64 bytesRemoved = 0;
    QStringList errors;

    void merge(const TrashCleanupResult &other);
};

TrashCleanupResult cleanTrashRoot(const QString &trashRoot);

}
