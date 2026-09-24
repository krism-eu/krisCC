#include <QtTest>

#include "Validators.h"

class ValidatorsTest final : public QObject
{
    Q_OBJECT
private slots:

    void archiveMembers()
    {
        QVERIFY(Validators::archiveMemberPath(QStringLiteral(".config/app/file.ini")));
        QVERIFY(!Validators::archiveMemberPath(QStringLiteral("/etc/passwd")));
        QVERIFY(!Validators::archiveMemberPath(QStringLiteral("../escape")));
        QVERIFY(!Validators::archiveMemberPath(QStringLiteral("safe/../escape")));
        QVERIFY(Validators::archiveVerboseEntry(QStringLiteral("-rw------- user/group 12 2026-09-24 00:00 .config/a")));
        QVERIFY(Validators::archiveVerboseEntry(QStringLiteral("drwx------ user/group 0 2026-09-24 00:00 .config/")));
        QVERIFY(Validators::archiveVerboseEntry(QStringLiteral("lrwxrwxrwx user/group 0 2026-09-24 00:00 .config/link -> target")));
        QVERIFY(!Validators::archiveVerboseEntry(QStringLiteral("hrw------- user/group 0 2026-09-24 00:00 hard link to x")));
        QVERIFY(!Validators::archiveVerboseEntry(QStringLiteral("prw------- user/group 0 2026-09-24 00:00 fifo")));
        QVERIFY(!Validators::archiveVerboseEntry(QStringLiteral("brw------- user/group 8,0 2026-09-24 00:00 dev")));
        QVERIFY(!Validators::archiveVerboseEntry(QStringLiteral("lrwxrwxrwx user/group 0 2026-09-24 00:00 link -> ../escape")));
    }
    void packageNames()
    {
        QVERIFY(Validators::packageName(QStringLiteral("tree")));
        QVERIFY(Validators::packageName(QStringLiteral("qt6-qtbase")));
        QVERIFY(Validators::packageName(QStringLiteral("foo+bar_baz")));
        QVERIFY(!Validators::packageName(QStringLiteral("foo:bar")));
        QVERIFY(!Validators::packageName(QStringLiteral("-tree")));
        QVERIFY(!Validators::packageName(QStringLiteral("tree.x86_64")));
        QVERIFY(!Validators::packageName(QStringLiteral("tree.rpm")));
        QVERIFY(!Validators::packageName(QStringLiteral("name with spaces")));
    }

    void repositoryUrls()
    {
        QVERIFY(Validators::repositoryUrl(QStringLiteral("https://example.org/repo.repo")));
        QVERIFY(!Validators::repositoryUrl(QStringLiteral("http://example.org/repo.repo")));
        QVERIFY(!Validators::repositoryUrl(QStringLiteral("https://user:pass@example.org/repo.repo")));
        QVERIFY(!Validators::repositoryUrl(QStringLiteral("file:///tmp/repo.repo")));
    }
    void bootValues()
    {
        QVERIFY(Validators::bootToken(QStringLiteral("00AF")));
        QVERIFY(!Validators::bootToken(QStringLiteral("00AF00")));
        QVERIFY(Validators::grubEntry(QStringLiteral("ostree-1")));
        QVERIFY(!Validators::grubEntry(QStringLiteral("--unrestricted")));
        QVERIFY(!Validators::grubEntry(QStringLiteral("bad\nentry")));
    }
};
QTEST_APPLESS_MAIN(ValidatorsTest)
#include "test_validators.moc"
