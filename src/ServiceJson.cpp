#include "ServiceJson.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QVariantMap>

namespace {
bool validService(const QString &unit)
{
    if (unit.size() > 255)
        return false;

    static const QRegularExpression pattern(
        QStringLiteral("^(?:[A-Za-z0-9_.@:-]|\\\\x[0-9A-Fa-f]{2})+\\.service$"));
    return pattern.match(unit).hasMatch();
}

ServiceJsonResult parse(const QByteArray &data, const QString &scope, bool files)
{
    ServiceJsonResult result;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        result.error = QStringLiteral("JSON systemd non valido: %1").arg(parseError.errorString());
        return result;
    }
    if (!document.isArray()) {
        result.error = QStringLiteral("JSON systemd con radice inattesa: atteso array.");
        return result;
    }

    int index = 0;
    for (const QJsonValue &value : document.array()) {
        if (!value.isObject()) {
            result.error = QStringLiteral("Voce systemd %1 non è un oggetto.").arg(index);
            return result;
        }
        const QJsonObject object = value.toObject();
        const QString unit = object.value(files ? QStringLiteral("unit_file")
                                                : QStringLiteral("unit")).toString();
        if (!validService(unit)) {
            result.error = QStringLiteral("Voce systemd %1 senza nome .service valido.").arg(index);
            return result;
        }

        QVariantMap row;
        row.insert(QStringLiteral("unit"), unit);
        row.insert(QStringLiteral("scope"), scope);
        if (files) {
            if (!object.value(QStringLiteral("state")).isString()) {
                result.error = QStringLiteral("Voce unit-file %1 senza stato valido.").arg(index);
                return result;
            }
            row.insert(QStringLiteral("load"), QStringLiteral("not-loaded"));
            row.insert(QStringLiteral("active"), QStringLiteral("not-loaded"));
            row.insert(QStringLiteral("sub"), QString());
            row.insert(QStringLiteral("description"), QString());
            row.insert(QStringLiteral("enabled"), object.value(QStringLiteral("state")).toString());
        } else {
            if (!object.value(QStringLiteral("active")).isString()
                || !object.value(QStringLiteral("sub")).isString()
                || !object.value(QStringLiteral("description")).isString()) {
                result.error = QStringLiteral("Voce unità %1 con campi obbligatori mancanti.").arg(index);
                return result;
            }

            const QString load = object.value(QStringLiteral("load")).isString()
                ? object.value(QStringLiteral("load")).toString()
                : QStringLiteral("unknown");
            const QString active = object.value(QStringLiteral("active")).toString();

            row.insert(QStringLiteral("load"), load);
            // A referenced-but-uninstalled unit is not an inactive service the
            // user can manage. Expose a dedicated UI state instead of painting
            // it as a failure/inactive service.
            row.insert(QStringLiteral("active"),
                       load == QStringLiteral("not-found")
                           ? QStringLiteral("missing")
                           : active);
            row.insert(QStringLiteral("sub"), object.value(QStringLiteral("sub")).toString());
            row.insert(QStringLiteral("description"), object.value(QStringLiteral("description")).toString());
            row.insert(QStringLiteral("enabled"), QStringLiteral("unknown"));
        }
        result.rows.append(row);
        ++index;
    }
    return result;
}
}

namespace ServiceJson {
ServiceJsonResult parseUnitList(const QByteArray &data, const QString &scope)
{
    return parse(data, scope, false);
}

ServiceJsonResult parseUnitFiles(const QByteArray &data, const QString &scope)
{
    return parse(data, scope, true);
}
}
