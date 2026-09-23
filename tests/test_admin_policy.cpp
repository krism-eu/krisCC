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
    }
    void rejectsUnexpectedInput()
    {
        QVERIFY(!AdminPolicy::resolve({}).has_value());
        QVERIFY(!AdminPolicy::resolve({QStringLiteral("shell"), QStringLiteral("id")}).has_value());
        QVERIFY(!AdminPolicy::resolve({QStringLiteral("rk-add"), QStringLiteral("foo:bar")}).has_value());
        QVERIFY(!AdminPolicy::resolve({QStringLiteral("repo-add"), QStringLiteral("http://example.org/test.repo")}).has_value());
        QVERIFY(!AdminPolicy::resolve({QStringLiteral("boot-next-uefi"), QStringLiteral("-o")}).has_value());
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
