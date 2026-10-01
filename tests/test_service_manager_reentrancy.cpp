#include <QtTest>
#include <QSignalSpy>

#define private public
#include "ServiceManagerBackend.h"
#undef private
#include "ProcessRunner.h"

class ServiceManagerReentrancyTest final : public QObject
{
    Q_OBJECT
private slots:
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
