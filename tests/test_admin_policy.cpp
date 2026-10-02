#include <QtTest>

#include <QFile>
#include <QHash>
#include <QXmlStreamReader>

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

    void polkitUefiCoverage()
    {
        struct Action {
            QString executable;
            QString argv1;
            QString allowAny;
            QString allowInactive;
            QString allowActive;
        };

        const QString policyPath = QFINDTESTDATA("../data/org.kriscc.controlcenter.policy");
        QVERIFY2(!policyPath.isEmpty(), "Polkit policy test data not found");
        QFile file(policyPath);
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));

        QHash<QString, Action> actions;
        QXmlStreamReader xml(&file);
        QString actionId;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.name() == QStringLiteral("action")) {
                actionId = xml.attributes().value(QStringLiteral("id")).toString();
                actions.insert(actionId, Action{});
                continue;
            }
            if (xml.isEndElement() && xml.name() == QStringLiteral("action")) {
                actionId.clear();
                continue;
            }
            if (!xml.isStartElement() || actionId.isEmpty())
                continue;

            Action &action = actions[actionId];
            if (xml.name() == QStringLiteral("annotate")) {
                const QString key = xml.attributes().value(QStringLiteral("key")).toString();
                const QString value = xml.readElementText().trimmed();
                if (key == QStringLiteral("org.freedesktop.policykit.exec.path"))
                    action.executable = value;
                else if (key == QStringLiteral("org.freedesktop.policykit.exec.argv1"))
                    action.argv1 = value;
            } else if (xml.name() == QStringLiteral("allow_any")) {
                action.allowAny = xml.readElementText().trimmed();
            } else if (xml.name() == QStringLiteral("allow_inactive")) {
                action.allowInactive = xml.readElementText().trimmed();
            } else if (xml.name() == QStringLiteral("allow_active")) {
                action.allowActive = xml.readElementText().trimmed();
            }
        }
        QVERIFY2(!xml.hasError(), qPrintable(xml.errorString()));

        const QHash<QString, QString> expected = {
            {QStringLiteral("org.kriscc.controlcenter.boot.read-uefi"), QStringLiteral("boot-read-uefi")},
            {QStringLiteral("org.kriscc.controlcenter.bootnext.clear-uefi"), QStringLiteral("boot-clear-next-uefi")},
            {QStringLiteral("org.kriscc.controlcenter.boot.delete-uefi"), QStringLiteral("boot-delete-uefi")},
            {QStringLiteral("org.kriscc.controlcenter.boot.order-uefi"), QStringLiteral("boot-order-uefi")}
        };

        for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
            QVERIFY2(actions.contains(it.key()), qPrintable(QStringLiteral("Missing action %1").arg(it.key())));
            const Action action = actions.value(it.key());
            QCOMPARE(action.executable, QStringLiteral("/usr/libexec/kriscc/admin"));
            QCOMPARE(action.argv1, it.value());
            QCOMPARE(action.allowAny, QStringLiteral("no"));
            QCOMPARE(action.allowInactive, QStringLiteral("no"));
            QCOMPARE(action.allowActive, QStringLiteral("auth_admin"));
        }

        for (const Action &action : actions)
            QVERIFY(action.allowActive != QStringLiteral("auth_admin_keep"));
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
        QVERIFY(!AdminPolicy::resolve({QStringLiteral("boot-order-uefi"), QStringLiteral("0001, 0002")}).has_value());
        QVERIFY(!AdminPolicy::resolve({QStringLiteral("boot-order-uefi"), QStringLiteral("0001,,0002")}).has_value());
        QVERIFY(!AdminPolicy::resolve({QStringLiteral("boot-order-uefi"), QStringLiteral(",0001")}).has_value());
        QVERIFY(!AdminPolicy::resolve({QStringLiteral("boot-order-uefi"), QStringLiteral("0001,")}).has_value());
        QVERIFY(!AdminPolicy::resolve({QStringLiteral("boot-order-uefi"), QStringLiteral(" 0001")}).has_value());
        QVERIFY(!AdminPolicy::resolve({QStringLiteral("boot-order-uefi"), QStringLiteral("0001 ")}).has_value());
        QVERIFY(!AdminPolicy::resolve({QStringLiteral("boot-next-grub"), QStringLiteral("--unrestricted")}).has_value());
        QVERIFY(!AdminPolicy::resolve({QStringLiteral("bootc-check"), QStringLiteral("--extra")}).has_value());
    }

    void adminHelperIgnoresZombieAndDeadTasks()
    {
        const QString helperPath = QFINDTESTDATA("../src/AdminHelper.cpp");
        QVERIFY2(!helperPath.isEmpty(), "AdminHelper source test data not found");
        QFile file(helperPath);
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const QByteArray source = file.readAll();
        QVERIFY(source.contains("const QByteArray &stateField = fields.at(0);"));
        QVERIFY(source.contains("state == 'Z' || state == 'X' || state == 'x'"));
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
