#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVariantList>

namespace ContractParsers {
enum class Error { None, InvalidJson, UnsupportedContract, InvalidShape, InvalidValue };

struct Rows {
    Error error = Error::None;
    QVariantList values;
    bool ok() const { return error == Error::None; }
};

struct RkStatus {
    Error error = Error::None;
    QString overlay;
    bool pendingRecovery = false;
    bool needsSync = false;
    QStringList requests;
    QString formatted;
    bool ok() const { return error == Error::None; }
};

struct BootcStatus {
    Error error = Error::None;
    QVariantList deployments;
    QString formatted;
    bool ok() const { return error == Error::None; }
};

RkStatus parseRkStatus(const QByteArray &data);
BootcStatus parseBootcStatus(const QByteArray &data);
Rows parseDnfRepoquery(const QByteArray &data);
Rows parseDnfListJson(const QByteArray &data);
Rows parseFlatpakTsv(const QByteArray &data, int expectedColumns);
Rows parsePodmanJson(const QByteArray &data);
Rows parseUefiEntries(const QByteArray &data);
Rows parseGrubbyEntries(const QByteArray &data);
}
