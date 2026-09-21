#include "../src/AdminPolicy.h"
#include "../src/PrivilegedProcessRunner.h"

#include <QElapsedTimer>
#include <QtTest/QTest>

class AdminPolicyTest final : public QObject
{
    Q_OBJECT

private slots:
    void allowsExactOperations()
    {
        AdminCommand command;

        QVERIFY(buildAdminCommand({QStringLiteral("admin"), QStringLiteral("rk-add"),
                                   QStringLiteral("tree")}, &command));
        QCOMPARE(command.program, QStringLiteral("/usr/bin/rk"));
        QCOMPARE(command.arguments, QStringList({QStringLiteral("add"), QStringLiteral("tree")}));

        QVERIFY(buildAdminCommand({QStringLiteral("admin"), QStringLiteral("repo-enable"),
                                   QStringLiteral("fedora")}, &command));
        QCOMPARE(command.program, QStringLiteral("/usr/bin/dnf5"));

        QVERIFY(buildAdminCommand({QStringLiteral("admin"), QStringLiteral("boot-next-uefi"),
                                   QStringLiteral("000a")}, &command));
        QCOMPARE(command.arguments, QStringList({QStringLiteral("-n"), QStringLiteral("000A")}));
    }

    void rejectsUnexpectedArguments()
    {
        AdminCommand command;
        QVERIFY(!buildAdminCommand({QStringLiteral("admin"), QStringLiteral("rk-add"),
                                    QStringLiteral("pkg:x86_64")}, &command));
        QVERIFY(!buildAdminCommand({QStringLiteral("admin"), QStringLiteral("repo-add"),
                                    QStringLiteral("http://example.org/repo.repo")}, &command));
        QVERIFY(!buildAdminCommand({QStringLiteral("admin"), QStringLiteral("boot-next-grub"),
                                    QStringLiteral("-bad")}, &command));
        QVERIFY(!buildAdminCommand({QStringLiteral("admin"), QStringLiteral("bootc-check"),
                                    QStringLiteral("--extra")}, &command));
        QVERIFY(!buildAdminCommand({QStringLiteral("admin"), QStringLiteral("unknown")}, &command));
    }

    void processRunnerReturnsChildStatus()
    {
        QCOMPARE(runPrivilegedCommand(QStringLiteral("/usr/bin/true"), {}, 1000), 0);
        QCOMPARE(runPrivilegedCommand(QStringLiteral("/usr/bin/false"), {}, 1000), 1);
    }

    void processRunnerEnforcesTimeout()
    {
        QElapsedTimer timer;
        timer.start();
        QCOMPARE(runPrivilegedCommand(QStringLiteral("/usr/bin/sleep"),
                                      {QStringLiteral("10")}, 150), 124);
        QVERIFY2(timer.elapsed() < 5000, "timeout did not stop the child promptly");
    }
};

QTEST_MAIN(AdminPolicyTest)
#include "test_admin_policy.moc"
