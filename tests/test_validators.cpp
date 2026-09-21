#include "../src/Validators.h"

#include <QtTest/QTest>

class ValidatorsTest final : public QObject
{
    Q_OBJECT

private slots:
    void packageNames()
    {
        QVERIFY(Validators::packageName(QStringLiteral("tree")));
        QVERIFY(Validators::packageName(QStringLiteral("python3-requests")));
        QVERIFY(!Validators::packageName(QStringLiteral("pkg:x86_64")));
        QVERIFY(!Validators::packageName(QStringLiteral("tree.x86_64")));
        QVERIFY(!Validators::packageName(QStringLiteral("../tree")));
        QVERIFY(!Validators::packageName(QStringLiteral("-tree")));
    }

    void repositoryIds()
    {
        QVERIFY(Validators::repositoryId(QStringLiteral("fedora-cisco-openh264")));
        QVERIFY(!Validators::repositoryId(QStringLiteral("../fedora")));
        QVERIFY(!Validators::repositoryId(QStringLiteral("fedora repo")));
    }

    void repositoryUrls()
    {
        QVERIFY(Validators::repositoryUrl(QStringLiteral("https://example.org/repo.repo")));
        QVERIFY(!Validators::repositoryUrl(QStringLiteral("http://example.org/repo.repo")));
        QVERIFY(!Validators::repositoryUrl(QStringLiteral("https://user:secret@example.org/repo.repo")));
        QVERIFY(!Validators::repositoryUrl(QStringLiteral("https://example.org/a\n.repo")));
    }

    void bootValues()
    {
        QVERIFY(Validators::bootToken(QStringLiteral("000A")));
        QVERIFY(!Validators::bootToken(QStringLiteral("000AA")));
        QVERIFY(Validators::grubEntry(QStringLiteral("Fedora Linux")));
        QVERIFY(!Validators::grubEntry(QStringLiteral("-bad")));
        QVERIFY(!Validators::grubEntry(QStringLiteral("bad\nentry")));
    }
};

QTEST_MAIN(ValidatorsTest)
#include "test_validators.moc"
