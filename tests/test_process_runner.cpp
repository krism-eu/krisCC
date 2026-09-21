#include <QtTest>
#include <QSignalSpy>
#include <QTimer>

#include "ProcessRunner.h"

class ProcessRunnerTest final : public QObject
{
    Q_OBJECT

private slots:
    void stdinIsClosed()
    {
        ProcessRunner runner;
        QSignalSpy spy(&runner, &ProcessRunner::finished);
        ProcessRunner::Options options;
        options.program = QStringLiteral("/usr/bin/bash");
        options.arguments = {QStringLiteral("--noprofile"), QStringLiteral("--norc"),
                             QStringLiteral("-c"), QStringLiteral("read value; test $? -ne 0")};
        options.timeoutMs = 2000;
        QVERIFY(runner.start(options));
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.at(0).at(0).value<ProcessRunner::Outcome>(), ProcessRunner::Success);
    }

    void timeoutIsReal()
    {
        ProcessRunner runner;
        QSignalSpy spy(&runner, &ProcessRunner::finished);
        ProcessRunner::Options options;
        options.program = QStringLiteral("/usr/bin/bash");
        options.arguments = {QStringLiteral("-c"), QStringLiteral("sleep 10")};
        options.timeoutMs = 100;
        QVERIFY(runner.start(options));
        QVERIFY(spy.wait(4000));
        QCOMPARE(spy.at(0).at(0).value<ProcessRunner::Outcome>(), ProcessRunner::TimedOut);
    }

    void cancelIsDistinct()
    {
        ProcessRunner runner;
        QSignalSpy spy(&runner, &ProcessRunner::finished);
        ProcessRunner::Options options;
        options.program = QStringLiteral("/usr/bin/bash");
        options.arguments = {QStringLiteral("-c"), QStringLiteral("sleep 10")};
        options.timeoutMs = 5000;
        QVERIFY(runner.start(options));
        QTimer::singleShot(50, &runner, [&runner] { QVERIFY(runner.cancel()); });
        QVERIFY(spy.wait(4000));
        QCOMPARE(spy.at(0).at(0).value<ProcessRunner::Outcome>(), ProcessRunner::Cancelled);
    }

    void failedStartIsDistinct()
    {
        ProcessRunner runner;
        QSignalSpy spy(&runner, &ProcessRunner::finished);
        ProcessRunner::Options options;
        options.program = QStringLiteral("/definitely/not/a/real/program");
        options.timeoutMs = 1000;
        QVERIFY(runner.start(options));
        QVERIFY(spy.wait(2000));
        QCOMPARE(spy.at(0).at(0).value<ProcessRunner::Outcome>(), ProcessRunner::FailedToStart);
    }

    void outputIsBounded()
    {
        ProcessRunner runner;
        QSignalSpy spy(&runner, &ProcessRunner::finished);
        ProcessRunner::Options options;
        options.program = QStringLiteral("/usr/bin/bash");
        options.arguments = {QStringLiteral("-c"), QStringLiteral("printf '%01024d' 0")};
        options.maxOutputBytes = 64;
        options.timeoutMs = 1000;
        QVERIFY(runner.start(options));
        QVERIFY(spy.wait(2000));
        QCOMPARE(spy.at(0).at(0).value<ProcessRunner::Outcome>(), ProcessRunner::Success);
        const QByteArray output = spy.at(0).at(2).toByteArray();
        QVERIFY(output.size() <= 64);
        QVERIFY(!output.isEmpty());
    }
};

Q_DECLARE_METATYPE(ProcessRunner::Outcome)
QTEST_APPLESS_MAIN(ProcessRunnerTest)
#include "test_process_runner.moc"
