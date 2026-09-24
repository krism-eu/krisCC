#include "ContractParsers.h"
#include "Validators.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>

namespace {
QString jsonString(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString() : QString();
}

QVariantMap deploymentMap(const QString &role, const QJsonObject &deployment)
{
    QVariantMap map;
    map.insert(QStringLiteral("role"), role);

    const QJsonObject imageStatus = deployment.value(QStringLiteral("image")).toObject();
    const QJsonObject imageReference = imageStatus.value(QStringLiteral("image")).toObject();
    const QJsonObject ostree = deployment.value(QStringLiteral("ostree")).toObject();

    QString image = jsonString(imageReference, QStringLiteral("image"));
    if (image.isEmpty()) image = jsonString(imageReference, QStringLiteral("reference"));
    if (image.isEmpty() && imageStatus.value(QStringLiteral("image")).isString())
        image = imageStatus.value(QStringLiteral("image")).toString();
    if (image.isEmpty() && deployment.value(QStringLiteral("image")).isString())
        image = deployment.value(QStringLiteral("image")).toString();

    QString version = jsonString(imageStatus, QStringLiteral("version"));
    if (version.isEmpty()) version = jsonString(deployment, QStringLiteral("version"));

    QString digest = jsonString(imageStatus, QStringLiteral("imageDigest"));
    if (digest.isEmpty()) digest = jsonString(imageReference, QStringLiteral("imageDigest"));
    if (digest.isEmpty()) digest = jsonString(imageReference, QStringLiteral("digest"));
    if (digest.isEmpty()) digest = jsonString(deployment, QStringLiteral("imageDigest"));

    QString checksum = jsonString(ostree, QStringLiteral("checksum"));
    if (checksum.isEmpty()) checksum = jsonString(deployment, QStringLiteral("checksum"));

    QString timestamp = jsonString(imageStatus, QStringLiteral("timestamp"));
    if (timestamp.isEmpty()) timestamp = jsonString(deployment, QStringLiteral("timestamp"));

    map.insert(QStringLiteral("image"), image);
    map.insert(QStringLiteral("version"), version);
    map.insert(QStringLiteral("digest"), digest);
    map.insert(QStringLiteral("checksum"), checksum);
    map.insert(QStringLiteral("pinned"), deployment.value(QStringLiteral("pinned")).toBool(false));
    map.insert(QStringLiteral("downloadOnly"), deployment.value(QStringLiteral("downloadOnly")).toBool(false));
    map.insert(QStringLiteral("timestamp"), timestamp);
    return map;
}
}

ContractParsers::RkStatus ContractParsers::parseRkStatus(const QByteArray &data)
{
    RkStatus result;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = Error::InvalidJson;
        return result;
    }

    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("schema")).toInt(-1) != 1) {
        result.error = Error::UnsupportedContract;
        return result;
    }
    if (!object.value(QStringLiteral("overlay")).isString()
        || !object.value(QStringLiteral("pending_recovery")).isBool()
        || !object.value(QStringLiteral("needs_sync")).isBool()
        || !object.value(QStringLiteral("requests")).isArray()) {
        result.error = Error::InvalidShape;
        return result;
    }

    const QString overlay = object.value(QStringLiteral("overlay")).toString();
    if (overlay != QStringLiteral("ready") && overlay != QStringLiteral("degraded")) {
        result.error = Error::InvalidValue;
        return result;
    }

    for (const QJsonValue &value : object.value(QStringLiteral("requests")).toArray()) {
        if (!value.isString() || !Validators::packageName(value.toString())) {
            result.error = Error::InvalidValue;
            return result;
        }
        result.requests.append(value.toString());
    }

    result.overlay = overlay;
    result.pendingRecovery = object.value(QStringLiteral("pending_recovery")).toBool();
    result.needsSync = object.value(QStringLiteral("needs_sync")).toBool();
    result.formatted = QString::fromUtf8(document.toJson(QJsonDocument::Indented)).trimmed();
    return result;
}

ContractParsers::BootcStatus ContractParsers::parseBootcStatus(const QByteArray &data)
{
    BootcStatus result;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = Error::InvalidJson;
        return result;
    }

    const QJsonObject root = document.object();    if (root.value(QStringLiteral("apiVersion")).toString() != QStringLiteral("org.containers.bootc/v1")
        || root.value(QStringLiteral("kind")).toString() != QStringLiteral("BootcHost")) {
        result.error = Error::UnsupportedContract;
        return result;
    }
    if (!root.value(QStringLiteral("status")).isObject()) {
        result.error = Error::InvalidShape;
        return result;
    }

    const QJsonObject status = root.value(QStringLiteral("status")).toObject();
    const struct { const char *key; const char *label; } roles[] = {
        {"staged", "Staged"}, {"booted", "Booted"}, {"rollback", "Rollback"}
    };
    for (const auto &role : roles) {
        const QJsonValue value = status.value(QLatin1String(role.key));
        if (value.isNull() || value.isUndefined())
            continue;
        if (!value.isObject()) {
            result.error = Error::InvalidShape;
            result.deployments.clear();
            return result;
        }
        const QJsonObject deployment = value.toObject();
        if (!deployment.isEmpty())
            result.deployments.append(deploymentMap(QLatin1String(role.label), deployment));
    }

    result.formatted = QString::fromUtf8(document.toJson(QJsonDocument::Indented)).trimmed();
    return result;
}

ContractParsers::Rows ContractParsers::parseDnfRepoquery(const QByteArray &data)
{
    Rows result;
    QSet<QString> seen;
    for (const QString &line : QString::fromUtf8(data).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QStringList parts = line.split(QLatin1Char('\t'), Qt::KeepEmptyParts);
        if (parts.size() < 7) {
            result.error = Error::InvalidShape;
            result.values.clear();
            return result;
        }

        const qsizetype n = parts.size();
        bool downloadOk = false;
        bool installOk = false;
        const quint64 downloadSize = parts.at(n - 2).toULongLong(&downloadOk);
        const quint64 installSize = parts.at(n - 1).toULongLong(&installOk);
        const QString name = parts.at(0).trimmed();
        const QString arch = parts.at(n - 3).trimmed();
        if (name.isEmpty() || arch.isEmpty() || !downloadOk || !installOk) {
            result.error = Error::InvalidValue;
            result.values.clear();
            return result;
        }

        const QString key = name + QLatin1Char('\x1f') + arch;
        if (seen.contains(key))
            continue;
        seen.insert(key);

        QVariantMap row;
        row.insert(QStringLiteral("name"), name);
        row.insert(QStringLiteral("summary"), parts.mid(1, n - 6).join(QLatin1Char('\t')).simplified().left(512));
        row.insert(QStringLiteral("version"), parts.at(n - 5).trimmed());
        row.insert(QStringLiteral("repository"), parts.at(n - 4).trimmed());
        row.insert(QStringLiteral("arch"), arch);
        row.insert(QStringLiteral("downloadSize"), QVariant::fromValue(downloadSize));
        row.insert(QStringLiteral("installSize"), QVariant::fromValue(installSize));
        result.values.append(row);
    }
    return result;
}

ContractParsers::Rows ContractParsers::parseDnfListJson(const QByteArray &data)
{
    Rows result;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = Error::InvalidJson;
        return result;
    }

    bool sawArray = false;
    QSet<QString> seen;
    const QJsonObject root = document.object();
    if (root.isEmpty())
        return result;
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        if (!it.value().isArray())
            continue;
        sawArray = true;
        for (const QJsonValue &value : it.value().toArray()) {
            if (!value.isObject()) {
                result.error = Error::InvalidShape;
                result.values.clear();
                return result;
            }
            const QJsonObject object = value.toObject();
            if (!object.value(QStringLiteral("name")).isString()
                || !object.value(QStringLiteral("arch")).isString()
                || !object.value(QStringLiteral("evr")).isString()
                || !object.value(QStringLiteral("repository")).isString()) {
                result.error = Error::InvalidShape;
                result.values.clear();
                return result;
            }

            const QString name = object.value(QStringLiteral("name")).toString().trimmed();
            const QString arch = object.value(QStringLiteral("arch")).toString().trimmed();
            if (name.isEmpty() || arch.isEmpty()) {
                result.error = Error::InvalidValue;
                result.values.clear();
                return result;
            }

            const QString key = name + QLatin1Char('\x1f') + arch;
            if (seen.contains(key))
                continue;
            seen.insert(key);

            QVariantMap row;
            row.insert(QStringLiteral("name"), name);
            row.insert(QStringLiteral("arch"), arch);
            row.insert(QStringLiteral("version"), object.value(QStringLiteral("evr")).toString());
            row.insert(QStringLiteral("repository"), object.value(QStringLiteral("repository")).toString());
            result.values.append(row);
        }
    }
    if (!sawArray)
        result.error = Error::InvalidShape;
    return result;
}

ContractParsers::Rows ContractParsers::parseUefiEntries(const QByteArray &data)
{
    Rows result;
    static const QRegularExpression pattern(QStringLiteral("^Boot([0-9A-Fa-f]{4})\\*?\\s+(.+)$"));
    for (const QString &raw : QString::fromUtf8(data).split(QLatin1Char('\n'))) {
        const QRegularExpressionMatch match = pattern.match(raw.trimmed());
        if (!match.hasMatch())
            continue;
        QString description = match.captured(2).trimmed();
        const qsizetype tab = description.indexOf(QLatin1Char('\t'));
        if (tab >= 0)
            description = description.left(tab).trimmed();
        if (description.isEmpty())
            continue;
        const QString code = match.captured(1).toUpper();
        QVariantMap row;
        row.insert(QStringLiteral("code"), code);
        row.insert(QStringLiteral("label"), code + QStringLiteral(" · ") + description);
        result.values.append(row);
    }
    return result;
}

ContractParsers::Rows ContractParsers::parseGrubbyEntries(const QByteArray &data)
{
    Rows result;
    QString id;
    QString title;
    const auto commit = [&result, &id, &title]() {
        if (id.isEmpty())
            return;
        QVariantMap row;
        row.insert(QStringLiteral("id"), id);
        row.insert(QStringLiteral("label"), title.isEmpty() ? id : title);
        result.values.append(row);
        id.clear();
        title.clear();
    };

    for (const QString &raw : QString::fromUtf8(data).split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.startsWith(QStringLiteral("index=")))
            commit();
        else if (line.startsWith(QStringLiteral("title=")))
            title = line.mid(6).remove(QLatin1Char('"'));
        else if (line.startsWith(QStringLiteral("id=")))
            id = line.mid(3).remove(QLatin1Char('"'));
    }
    commit();
    return result;
}
