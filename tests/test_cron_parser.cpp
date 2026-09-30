#include <QtTest>

#include "CronParser.h"

class CronParserTest : public QObject
{
    Q_OBJECT

private slots:
    void parsesUserCrontab()
    {
        const QString text = QStringLiteral(
            "# comment\n"
            "SHELL=/bin/bash\n"
            "0 8 * * * ~/scripts/backup.sh --quiet\n"
            "*/15 * * * * sync-tool\n"
            "@reboot $HOME/bin/session-setup\n");

        const QList<CronParser::Entry> entries = CronParser::parse(text, false);
        QCOMPARE(entries.size(), 3);
        QCOMPARE(entries.at(0).schedule, QStringLiteral("0 8 * * *"));
        QCOMPARE(entries.at(0).command, QStringLiteral("~/scripts/backup.sh --quiet"));
        QVERIFY(entries.at(0).user.isEmpty());
        QCOMPARE(entries.at(2).schedule, QStringLiteral("@reboot"));
        QCOMPARE(entries.at(2).command, QStringLiteral("$HOME/bin/session-setup"));
    }

    void parsesSystemCrontab()
    {
        const QString text = QStringLiteral(
            "0 4 * * * root /usr/local/bin/backup\n"
            "@daily nobody /usr/local/bin/cleanup --safe\n");

        const QList<CronParser::Entry> entries = CronParser::parse(text, true);
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries.at(0).user, QStringLiteral("root"));
        QCOMPARE(entries.at(0).command, QStringLiteral("/usr/local/bin/backup"));
        QCOMPARE(entries.at(1).schedule, QStringLiteral("@daily"));
        QCOMPARE(entries.at(1).user, QStringLiteral("nobody"));
        QCOMPARE(entries.at(1).command, QStringLiteral("/usr/local/bin/cleanup --safe"));
    }

    void describesCommonSchedules()
    {
        QCOMPARE(CronParser::describe(QStringLiteral("* * * * *")),
                 QStringLiteral("Ogni minuto"));
        QCOMPARE(CronParser::describe(QStringLiteral("*/15 * * * *")),
                 QStringLiteral("Ogni 15 minuti"));
        QCOMPARE(CronParser::describe(QStringLiteral("0 8 * * *")),
                 QStringLiteral("Ogni giorno alle 08:00"));
        QCOMPARE(CronParser::describe(QStringLiteral("0 3 * * 0")),
                 QStringLiteral("Ogni domenica alle 03:00"));
        QCOMPARE(CronParser::describe(QStringLiteral("@reboot")),
                 QStringLiteral("All'avvio"));
    }

    void leavesComplexSchedulesReadable()
    {
        const QString schedule = QStringLiteral("5 1-5 * 1,6 1-5");
        QCOMPARE(CronParser::describe(schedule), schedule);
    }
};

QTEST_MAIN(CronParserTest)
#include "test_cron_parser.moc"
