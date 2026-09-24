#include <QtTest>

#include "Validators.h"

class ValidatorsTest final : public QObject
{
    Q_OBJECT
private slots:
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
    void containerImageRefs()
    {
        QVERIFY(Validators::containerImageRef(QStringLiteral("sha256:7b52401234567890abcdef")));
        QVERIFY(Validators::containerImageRef(QStringLiteral("localhost:5000/app:1.0")));
        QVERIFY(Validators::containerImageRef(QStringLiteral("registry.example.org/team/app@sha256:0123456789abcdef")));
        QVERIFY(!Validators::containerImageRef(QStringLiteral("--force")));
        QVERIFY(!Validators::containerImageRef(QStringLiteral("bad image")));
        QVERIFY(!Validators::containerImageRef(QStringLiteral("image;rm")));
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
