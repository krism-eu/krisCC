#pragma once

#include <QString>
#include <QVariantList>

struct ServiceJsonResult
{
    QVariantList rows;
    QString error;
    bool ok() const { return error.isEmpty(); }
};

namespace ServiceJson {
ServiceJsonResult parseUnitList(const QByteArray &data, const QString &scope);
ServiceJsonResult parseUnitFiles(const QByteArray &data, const QString &scope);
}
