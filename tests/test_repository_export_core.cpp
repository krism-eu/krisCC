#include <QtTest>
#include "RepositoryExportCore.h"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

class RepositoryExportCoreTest final : public QObject
{
    Q_OBJECT
private slots:
    void repositoryAllowlist()
    {
        QCOMPARE(RepositoryExportCore::repositorySlug(QStringLiteral("krisCC")), QStringLiteral("krism-eu/krisCC"));
        QCOMPARE(RepositoryExportCore::repositorySlug(QStringLiteral("KrisOS")), QStringLiteral("krism-eu/KrisOS"));
        QCOMPARE(RepositoryExportCore::workflowName(QStringLiteral("krisCC")), QStringLiteral("Build"));
        QCOMPARE(RepositoryExportCore::workflowName(QStringLiteral("KrisOS")), QStringLiteral("Build M1"));
        QVERIFY(RepositoryExportCore::repositorySlug(QStringLiteral("other")).isEmpty());
    }
    void latestGreenFixture()
    {
        const QByteArray json = R"({"workflow_runs":[
          {"name":"Other","status":"completed","conclusion":"success","head_branch":"wrong","head_sha":"1111111111111111111111111111111111111111"},
          {"name":"Build","status":"completed","conclusion":"success","head_branch":"0.7","head_sha":"abcdefabcdefabcdefabcdefabcdefabcdefabcd"},
          {"name":"Build","status":"completed","conclusion":"success","head_branch":"old","head_sha":"2222222222222222222222222222222222222222"}]})";
        QString error;
        const auto run = RepositoryExportCore::parseLatestGreenRun(json, QStringLiteral("krisCC"), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(run.valid());
        QCOMPARE(run.branch, QStringLiteral("0.7"));
        QCOMPARE(run.sha, QStringLiteral("abcdefabcdefabcdefabcdefabcdefabcdefabcd"));
        QVERIFY(!RepositoryExportCore::parseLatestGreenRun(
            R"({"workflow_runs":[]})", QStringLiteral("krisCC"), &error).valid());
        QVERIFY(!error.isEmpty());
    }
    void combinedExport()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString source = QDir(temp.path()).filePath(QStringLiteral("source"));
        QVERIFY(QDir().mkpath(QDir(source).filePath(QStringLiteral(".github/workflows"))));
        QFile script(QDir(source).filePath(QStringLiteral("script.sh")));
        QVERIFY(script.open(QIODevice::WriteOnly));
        QCOMPARE(script.write("#!/bin/sh\necho ok\n"), qint64(18));
        script.close();
        QFile hidden(QDir(source).filePath(QStringLiteral(".github/workflows/test.yml")));
        QVERIFY(hidden.open(QIODevice::WriteOnly));
        QVERIFY(hidden.write("name: test\n") > 0);
        hidden.close();
        QFile binary(QDir(source).filePath(QStringLiteral("image.bin")));
        QVERIFY(binary.open(QIODevice::WriteOnly));
        QCOMPARE(binary.write(QByteArray("abc\0def", 7)), qint64(7));
        binary.close();

        const QString destination = QDir(temp.path()).filePath(QStringLiteral("export.txt"));
        QString error;
        int textFiles = 0, binaryFiles = 0;
        const QString sha = QStringLiteral("abcdefabcdefabcdefabcdefabcdefabcdefabcd");
        QVERIFY2(RepositoryExportCore::writeCombinedRepository(
                     source, QStringLiteral("krisCC"), QStringLiteral("0.7"), sha,
                     destination, &textFiles, &binaryFiles, &error), qPrintable(error));
        QCOMPARE(textFiles, 2);
        QCOMPARE(binaryFiles, 1);
        QFile output(destination);
        QVERIFY(output.open(QIODevice::ReadOnly));
        const QByteArray data = output.readAll();
        QVERIFY(data.contains("# source: latest successful GitHub Actions build"));
        QVERIFY(data.contains("# branch: 0.7"));
        QVERIFY(data.contains("# commit: abcdefabcdefabcdefabcdefabcdefabcdefabcd"));
        QVERIFY(data.contains("===== FILE: script.sh ====="));
        QVERIFY(data.contains("===== FILE: .github/workflows/test.yml ====="));
        QVERIFY(data.contains("[[BINARY FILE OMITTED"));
    }
};
QTEST_APPLESS_MAIN(RepositoryExportCoreTest)
#include "test_repository_export_core.moc"
