#include <QtTest>

#include "ServiceJson.h"

class ServiceJsonTest final : public QObject
{
    Q_OBJECT
private slots:
    void emptyArrayIsHealthy()
    {
        const ServiceJsonResult result = ServiceJson::parseUnitList("[]", QStringLiteral("system"));
        QVERIFY(result.ok());
        QVERIFY(result.rows.isEmpty());
    }

    void malformedAndWrongRootFail()
    {
        QVERIFY(!ServiceJson::parseUnitList("[", QStringLiteral("system")).ok());
        QVERIFY(!ServiceJson::parseUnitList("{}", QStringLiteral("system")).ok());
    }

    void missingFieldsFail()
    {
        const QByteArray json = R"([{"unit":"demo.service","active":"inactive"}])";
        QVERIFY(!ServiceJson::parseUnitList(json, QStringLiteral("system")).ok());
    }

    void escapedSystemdUnitNamesAreAccepted()
    {
        const QByteArray json =
            R"([{"unit":"systemd-fsck@dev-disk-by\\x2duuid-AAF2\\x2d59EB.service","active":"inactive","sub":"dead","description":"File System Check"}])";

        const ServiceJsonResult result =
            ServiceJson::parseUnitList(json, QStringLiteral("system"));

        QVERIFY(result.ok());
        QCOMPARE(result.rows.size(), 1);

        const QVariantMap row = result.rows.first().toMap();
        QCOMPARE(
            row.value(QStringLiteral("unit")).toString(),
            QStringLiteral(R"(systemd-fsck@dev-disk-by\x2duuid-AAF2\x2d59EB.service)"));
    }

    void longEscapedSystemdUnitNamesAreAccepted()
    {
        const QString unit =
            QStringLiteral("systemd-fsck@")
            + QString(150, QLatin1Char('a'))
            + QStringLiteral(R"(\x2duuid.service)");

        QVERIFY(unit.size() > 128);
        QVERIFY(unit.size() <= 255);

        QString jsonUnit = unit;
        jsonUnit.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));

        const QByteArray json =
            QStringLiteral(
                R"([{"unit":"%1","active":"inactive","sub":"dead","description":"Long escaped unit"}])")
                .arg(jsonUnit)
                .toUtf8();

        const ServiceJsonResult result =
            ServiceJson::parseUnitList(json, QStringLiteral("system"));

        QVERIFY(result.ok());
        QCOMPARE(result.rows.size(), 1);
        QCOMPARE(
            result.rows.first().toMap().value(QStringLiteral("unit")).toString(),
            unit);
    }

    void malformedSystemdEscapesStillFail()
    {
        const QByteArray badEscape =
            R"([{"unit":"bad\\q.service","active":"inactive","sub":"dead","description":"Bad"}])";
        const QByteArray badHex =
            R"([{"unit":"bad\\xZZ.service","active":"inactive","sub":"dead","description":"Bad"}])";

        QVERIFY(!ServiceJson::parseUnitList(
            badEscape, QStringLiteral("system")).ok());
        QVERIFY(!ServiceJson::parseUnitList(
            badHex, QStringLiteral("system")).ok());
    }

    void unitFilesAreDistinctFromLoadedUnits()
    {
        const QByteArray json = R"([{"unit_file":"demo.service","state":"disabled"}])";
        const ServiceJsonResult result = ServiceJson::parseUnitFiles(json, QStringLiteral("system"));
        QVERIFY(result.ok());
        QCOMPARE(result.rows.size(), 1);
        const QVariantMap row = result.rows.first().toMap();
        QCOMPARE(row.value(QStringLiteral("unit")).toString(), QStringLiteral("demo.service"));
        QCOMPARE(row.value(QStringLiteral("active")).toString(), QStringLiteral("not-loaded"));
        QCOMPARE(row.value(QStringLiteral("enabled")).toString(), QStringLiteral("disabled"));
    }
};

QTEST_APPLESS_MAIN(ServiceJsonTest)
#include "test_service_json.moc"
