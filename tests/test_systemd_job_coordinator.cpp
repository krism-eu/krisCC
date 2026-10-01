#include <QtTest>
#include <QSignalSpy>

#define private public
#include "SystemdJobCoordinator.h"
#undef private

class SystemdJobCoordinatorTest final : public QObject
{
    Q_OBJECT
private:
    static void arm(SystemdJobCoordinator *coordinator,
                    const QString &unit = QStringLiteral("demo.service"),
                    const QString &action = QStringLiteral("start"))
    {
        coordinator->m_unit = unit;
        coordinator->m_action = action;
        coordinator->m_active = true;
        coordinator->m_timeout.start(5000);
    }

private slots:
    void earlyJobRemovedIsConsumedAfterReply()
    {
        SystemdJobCoordinator coordinator;
        arm(&coordinator);
        QSignalSpy completed(&coordinator, &SystemdJobCoordinator::completed);

        coordinator.onJobRemoved(7, QDBusObjectPath(QStringLiteral("/job/7")),
                                 QStringLiteral("demo.service"), QStringLiteral("done"));
        QVERIFY(coordinator.busy());
        QCOMPARE(coordinator.m_earlyResults.value(QStringLiteral("/job/7")),
                 QStringLiteral("done"));

        coordinator.acceptJobPath(QStringLiteral("/job/7"));
        QCOMPARE(completed.count(), 1);
        QVERIFY(!coordinator.busy());
        QCOMPARE(completed.at(0).at(0).toString(), QStringLiteral("demo.service"));
        QCOMPARE(completed.at(0).at(1).toString(), QStringLiteral("start"));
        QVERIFY(completed.at(0).at(2).toBool());
    }

    void lateJobRemovedCompletesExactJobOnly()
    {
        SystemdJobCoordinator coordinator;
        arm(&coordinator);
        coordinator.acceptJobPath(QStringLiteral("/job/9"));
        QSignalSpy completed(&coordinator, &SystemdJobCoordinator::completed);

        coordinator.onJobRemoved(8, QDBusObjectPath(QStringLiteral("/job/8")),
                                 QStringLiteral("demo.service"), QStringLiteral("done"));
        QCOMPARE(completed.count(), 0);
        QVERIFY(coordinator.busy());

        coordinator.onJobRemoved(9, QDBusObjectPath(QStringLiteral("/job/9")),
                                 QStringLiteral("demo.service"), QStringLiteral("failed"));
        QCOMPARE(completed.count(), 1);
        QVERIFY(!coordinator.busy());
        QVERIFY(!completed.at(0).at(2).toBool());
    }

    void managerRestartFailsActiveOperation()
    {
        SystemdJobCoordinator coordinator;
        coordinator.m_owner = QStringLiteral(":1.20");
        arm(&coordinator);
        QSignalSpy completed(&coordinator, &SystemdJobCoordinator::completed);

        coordinator.onOwnerChanged(QStringLiteral("org.freedesktop.systemd1"),
                                   QStringLiteral(":1.20"), QStringLiteral(":1.21"));
        QCOMPARE(completed.count(), 1);
        QVERIFY(!coordinator.busy());
        QVERIFY(!completed.at(0).at(2).toBool());
        QVERIFY(completed.at(0).at(3).toString().contains(QStringLiteral("riavviato")));
        QVERIFY(!coordinator.m_subscribed);
    }

    void timeoutFinalizesBeforeCompletionSignal()
    {
        SystemdJobCoordinator coordinator;
        arm(&coordinator);
        bool finalBeforeSignal = false;
        connect(&coordinator, &SystemdJobCoordinator::completed, &coordinator,
                [&coordinator, &finalBeforeSignal](const QString &, const QString &, bool, const QString &) {
            finalBeforeSignal = !coordinator.busy()
                && coordinator.m_unit.isEmpty()
                && coordinator.m_action.isEmpty()
                && coordinator.m_jobPath.isEmpty();
        });
        QSignalSpy completed(&coordinator, &SystemdJobCoordinator::completed);
        coordinator.m_timeout.start(1);
        QVERIFY(completed.wait(1000));
        QVERIFY(finalBeforeSignal);
        QVERIFY(!completed.at(0).at(2).toBool());
    }
};

QTEST_GUILESS_MAIN(SystemdJobCoordinatorTest)
#include "test_systemd_job_coordinator.moc"
