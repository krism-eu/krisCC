#include <QtTest>
#include <QFile>
#include <QSignalSpy>
#include <QTimer>

#include "ProcessRunner.h"

namespace {
bool processIsRunning(qint64 pid)
{
    QFile statFile(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!statFile.open(QIODevice::ReadOnly))
        return false;

    const QByteArray line = statFile.readAll().trimmed();
    const qsizetype closeParen = line.lastIndexOf(')');
    if (closeParen < 0 || closeParen + 2 >= line.size())
        return false;

    const char state = line.at(closeParen + 2);
    return state != 'Z' && state != 'X' && state != 'x';
}
}

Q_DECLARE_METATYPE(ProcessRunner::Outcome)

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
        QVERIFY(!runner.outputTruncated());
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
        QVERIFY(spy.wait(5000));
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
        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.at(0).at(0).value<ProcessRunner::Outcome>(), ProcessRunner::Cancelled);
    }

    void cancellationOutlivesLeaderAndKillsChild()
    {
        ProcessRunner runner;
        QSignalSpy spy(&runner, &ProcessRunner::finished);
        QByteArray streamed;
        connect(&runner, &ProcessRunner::outputReady, &runner,
                [&streamed](const QByteArray &data) { streamed.append(data); });

        ProcessRunner::Options options;
        options.program = QStringLiteral("/usr/bin/bash");
        options.arguments = {
            QStringLiteral("--noprofile"), QStringLiteral("--norc"), QStringLiteral("-c"),
            QStringLiteral("trap 'exit 0' TERM; (trap '' TERM; sleep 30) & child=$!; printf '%s\\n' \"$child\"; wait")
        };
        options.timeoutMs = 10000;
        QVERIFY(runner.start(options));
        QTRY_VERIFY_WITH_TIMEOUT(streamed.contains('\n'), 2000);
        const qint64 childPid = streamed.trimmed().toLongLong();
        QVERIFY(childPid > 0);

        QVERIFY(runner.cancel());
        QVERIFY(spy.wait(6000));
        QCOMPARE(spy.at(0).at(0).value<ProcessRunner::Outcome>(), ProcessRunner::Cancelled);

        QVERIFY2(!processIsRunning(childPid),
                 "cancel completed while a descendant was still running");
    }

    void normalLeaderExitWaitsForDescendant()
    {
        ProcessRunner runner;
        QSignalSpy spy(&runner, &ProcessRunner::finished);
        ProcessRunner::Options options;
        options.program = QStringLiteral("/usr/bin/bash");
        options.arguments = {
            QStringLiteral("--noprofile"), QStringLiteral("--norc"), QStringLiteral("-c"),
            QStringLiteral("(sleep 0.25) & exit 0")
        };
        options.timeoutMs = 3000;
        QVERIFY(runner.start(options));
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.at(0).at(0).value<ProcessRunner::Outcome>(), ProcessRunner::Success);
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

    void separateChannelsPreserveStructuredStdout()
    {
        ProcessRunner runner;
        QSignalSpy spy(&runner, &ProcessRunner::finished);
        ProcessRunner::Options options;
        options.program = QStringLiteral("/usr/bin/bash");
        options.arguments = {
            QStringLiteral("-c"),
            QStringLiteral("printf 'flathub\\tFlathub\\thttps://dl.flathub.org/repo/\\t\\n'; printf 'warning only\\n' >&2")
        };
        options.mergedChannels = false;
        options.timeoutMs = 1000;
        QVERIFY(runner.start(options));
        QVERIFY(spy.wait(2000));
        QCOMPARE(spy.at(0).at(0).value<ProcessRunner::Outcome>(), ProcessRunner::Success);
        QCOMPARE(spy.at(0).at(2).toByteArray(),
                 QByteArray("flathub\tFlathub\thttps://dl.flathub.org/repo/\t\n"));
        QCOMPARE(spy.at(0).at(3).toByteArray(), QByteArray("warning only\n"));
        QVERIFY(!runner.outputTruncated());
    }

    void outputIsBoundedAndReported()
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
        QVERIFY(runner.outputTruncated());
    }

    void streamingOutputIsCompletePastRestoreLimit()
    {
        ProcessRunner runner;
        QSignalSpy finishedSpy(&runner, &ProcessRunner::finished);
        QByteArray streamed;
        connect(&runner, &ProcessRunner::outputReady, &runner,
                [&streamed](const QByteArray &data) { streamed.append(data); });

        ProcessRunner::Options options;
        options.program = QStringLiteral("/usr/bin/bash");
        options.arguments = {QStringLiteral("-c"), QStringLiteral("printf '%0327680d' 0")};
        options.maxOutputBytes = 64 * 1024;
        options.timeoutMs = 1000;
        QVERIFY(runner.start(options));
        QVERIFY(finishedSpy.wait(2000));

        QCOMPARE(finishedSpy.at(0).at(0).value<ProcessRunner::Outcome>(), ProcessRunner::Success);
        QCOMPARE(streamed.size(), 320 * 1024);
        QVERIFY(finishedSpy.at(0).at(2).toByteArray().size() <= 64 * 1024);
        QVERIFY(runner.outputTruncated());
    }
};

QTEST_GUILESS_MAIN(ProcessRunnerTest)
#include "test_process_runner.moc"
