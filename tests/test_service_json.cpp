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
