#include <QtTest>

#include "AdminPolicy.h"

class AdminPolicyTest final : public QObject
{
    Q_OBJECT
private slots:
    void allowsFixedOperations()
    {
        auto bootc = AdminPolicy::resolve({QStringLiteral("bootc-check")});
        QVERIFY(bootc.has_value());
        QCOMPARE(bootc->program, QStringLiteral("/usr/bin/bootc"));
        QCOMPARE(bootc->arguments, QStringList({QStringLiteral("upgrade"), QStringLiteral("--check")}));

        auto rk = AdminPolicy::resolve({QStringLiteral("rk-add"), QStringLiteral("tree")});
        QVERIFY(rk.has_value());
        QCOMPARE(rk->program, QStringLiteral("/usr/bin/rk"));
        QCOMPARE(rk->arguments, QStringList({QStringLiteral("add"), QStringLiteral("tree")}));

        auto repo = AdminPolicy::resolve({QStringLiteral("repo-add"),
                                          QStringLiteral("https://example.org/test.repo")});
        QVERIFY(repo.has_value());
        QCOMPARE(repo->program, QStringLiteral("/usr/bin/dnf5"));

        auto uefi = AdminPolicy::resolve({QStringLiteral("boot-next-uefi"),
                                          QStringLiteral("00af")});
        QVERIFY(uefi.has_value());
        QCOMPARE(uefi->arguments.last(), QStringLiteral("00AF"));

        auto readUefi = AdminPolicy::resolve({QStringLiteral("boot-read-uefi")});
        QVERIFY(readUefi.has_value());
        QCOMPARE(readUefi->program, QStringLiteral("/usr/bin/efibootmgr"));
        QVERIFY(readUefi->arguments.isEmpty());

        auto clearNext = AdminPolicy::resolve({QStringLiteral("boot-clear-next-uefi")});
        QVERIFY(clearNext.has_value());
        QCOMPARE(clearNext->arguments, QStringList({QStringLiteral("-N")}));

        auto deleteEntry = AdminPolicy::resolve({QStringLiteral("boot-delete-uefi"),
                                                 QStringLiteral("0007")});
        QVERIFY(deleteEntry.has_value());
        QCOMPARE(deleteEntry->arguments,
                 QStringList({QStringLiteral("-b"), QStringLiteral("0007"), QStringLiteral("-B")}));

        auto bootOrder = AdminPolicy::resolve({QStringLiteral("boot-order-uefi"),
                                               QStringLiteral("0001,00af,0007")});
        QVERIFY(bootOrder.has_value());
        QCOMPARE(bootOrder->arguments.last(), QStringLiteral("0001,00AF,0007"));
    }
    void rejectsUnexpectedInput()
    {
        QVERIFY(!AdminPolicy::resolve({}).has_value());
        QVERIFY(!AdminPolicy::resolve({QStringLiteral("shell"), QStringLiteral("id")}).has_value());
        QVERIFY(!AdminPolicy::resolve({QStringLiteral("rk-add"), QStringLiteral("foo:bar")}).has_value());
        QVERIFY(!AdminPolicy::resolve({QStringLiteral("repo-add"), QStringLiteral("http://example.org/test.repo")}).has_value());
        QVERIFY(!AdminPolicy::resolve({QStringLiteral("boot-next-uefi"), QStringLiteral("-o")}).has_value());
        QVERIFY(!AdminPolicy::resolve({QStringLiteral("boot-delete-uefi"), QStringLiteral("00001")}).has_value());
        QVERIFY(!AdminPolicy::resolve({QStringLiteral("boot-order-uefi"), QStringLiteral("0001,0001")}).has_value());
        QVERIFY(!AdminPolicy::resolve({QStringLiteral("boot-next-grub"), QStringLiteral("--unrestricted")}).has_value());
        QVERIFY(!AdminPolicy::resolve({QStringLiteral("bootc-check"), QStringLiteral("--extra")}).has_value());
    }
    void timeoutsAreBoundedByDomain()
    {
        const auto bootc = AdminPolicy::resolve({QStringLiteral("bootc-check")});
        const auto repo = AdminPolicy::resolve({QStringLiteral("repo-enable"), QStringLiteral("fedora")});
        const auto nextBoot = AdminPolicy::resolve({QStringLiteral("boot-next-uefi"), QStringLiteral("0001")});
        QVERIFY(bootc && repo && nextBoot);
        QCOMPARE(bootc->timeoutMs, 30 * 60 * 1000);
        QCOMPARE(repo->timeoutMs, 5 * 60 * 1000);
        QCOMPARE(nextBoot->timeoutMs, 2 * 60 * 1000);
    }
};
QTEST_APPLESS_MAIN(AdminPolicyTest)
#include "test_admin_policy.moc"
