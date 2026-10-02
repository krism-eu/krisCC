#include <QtTest>
#include <QSignalSpy>

#include "ServiceManagerBackend.h"
#include "ProcessRunner.h"

class ServiceManagerReentrancyTest final : public QObject
{
    Q_OBJECT
private slots:
    void escapedSystemdUnitIsAccepted()
    {
        ServiceManagerBackend backend;

        const QString unit =
            QStringLiteral(R"(systemd-fsck@dev-disk-by\x2duuid-AAF2\x2d59EB.service)");

        QVERIFY(backend.validUnit(unit));
        QVERIFY(backend.validJournalUnit(unit));

        const QString longUnit =
            QStringLiteral("systemd-fsck@")
            + QString(150, QLatin1Char('a'))
            + QStringLiteral(R"(\x2duuid.service)");

        QVERIFY(longUnit.size() > 128);
        QVERIFY(longUnit.size() <= 255);
        QVERIFY(backend.validUnit(longUnit));
        QVERIFY(backend.validJournalUnit(longUnit));

        QVERIFY(!backend.validUnit(QStringLiteral(R"(bad\q.service)")));
        QVERIFY(!backend.validUnit(QStringLiteral(R"(bad\xZZ.service)")));
    }

    void activeServicesStaySeparateFromFullInventory()
    {
        ServiceManagerBackend backend;
        backend.m_userScope = false;

        QVariantMap completeOnly;
        completeOnly.insert(QStringLiteral("unit"),
                            QStringLiteral("complete-only.service"));
        backend.m_services = {completeOnly};

        const QByteArray json =
            R"([{"unit":"alpha.service","active":"active","sub":"running","description":"Alpha"},)"
            R"({"unit":"broken.service","active":"failed","sub":"failed","description":"Broken"}])";

        backend.handleFinished(ServiceManagerBackend::Task::ActiveServices, 0,
                               int(ProcessRunner::Success),
                               json, {}, {}, false);

        QCOMPARE(backend.activeServices().size(), 2);
        QCOMPARE(backend.services().size(), 1);

        QCOMPARE(
            backend.services().first().toMap()
                .value(QStringLiteral("unit")).toString(),
            QStringLiteral("complete-only.service"));

        QCOMPARE(backend.state(), QStringLiteral("success"));
    }

    void successStateIsFinalBeforeControlSignal()
    {
        ServiceManagerBackend backend;
        backend.m_busy = true;
        backend.m_userScope = false;
        backend.m_controlUnit = QStringLiteral("demo.service");
        backend.m_controlAction = QStringLiteral("restart");

        bool finalBeforeSignal = false;
        connect(&backend, &ServiceManagerBackend::controlFinished, &backend,
                [&backend, &finalBeforeSignal](bool, const QString &, const QString &, bool) {
            finalBeforeSignal = !backend.busy()
                && backend.state() == QStringLiteral("success")
                && backend.m_controlUnit.isEmpty()
                && backend.m_controlAction.isEmpty();
        });
        QSignalSpy control(&backend, &ServiceManagerBackend::controlFinished);

        backend.handleFinished(ServiceManagerBackend::Task::Control, 0,
                               int(ProcessRunner::Success), {}, {}, {}, false);
        QCOMPARE(control.count(), 1);
        QVERIFY(finalBeforeSignal);
        QVERIFY(control.at(0).at(3).toBool());
    }

    void failureStateIsFinalBeforeControlSignal()
    {
        ServiceManagerBackend backend;
        backend.m_busy = true;
        backend.m_userScope = true;
        backend.m_controlUnit = QStringLiteral("demo.service");
        backend.m_controlAction = QStringLiteral("start");

        bool finalBeforeSignal = false;
        connect(&backend, &ServiceManagerBackend::controlFinished, &backend,
                [&backend, &finalBeforeSignal](bool, const QString &, const QString &, bool) {
            finalBeforeSignal = !backend.busy()
                && backend.state() == QStringLiteral("error")
                && backend.m_controlUnit.isEmpty()
                && backend.m_controlAction.isEmpty();
        });
        QSignalSpy control(&backend, &ServiceManagerBackend::controlFinished);

        backend.handleFinished(ServiceManagerBackend::Task::Control, 1,
                               int(ProcessRunner::ExitError), {}, QByteArray("failed"), {}, false);
        QCOMPARE(control.count(), 1);
        QVERIFY(finalBeforeSignal);
        QVERIFY(!control.at(0).at(3).toBool());
    }
};

QTEST_GUILESS_MAIN(ServiceManagerReentrancyTest)
#include "test_service_manager_reentrancy.moc"
