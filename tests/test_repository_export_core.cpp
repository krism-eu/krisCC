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
        QCOMPARE(RepositoryExportCore::repositorySlug(QStringLiteral("krisCC")),
                 QStringLiteral("krism-eu/krisCC"));
        QCOMPARE(RepositoryExportCore::repositorySlug(QStringLiteral("KrisOS")),
                 QStringLiteral("krism-eu/KrisOS"));
        QVERIFY(RepositoryExportCore::repositorySlug(QStringLiteral("other")).isEmpty());
    }

    void branchFixture()
    {
        const QByteArray json = R"([{"name":"trial/ui"},{"name":"main"},{"name":"trial/ui"}])";
        QString error;
        const QStringList branches = RepositoryExportCore::parseBranchList(json, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(branches, QStringList({QStringLiteral("main"), QStringLiteral("trial/ui")}));
        QVERIFY(RepositoryExportCore::parseBranchList("{", &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QCOMPARE(RepositoryExportCore::safeFileComponent(QStringLiteral("trial/ui")),
                 QStringLiteral("trial_ui"));
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
        int textFiles = 0;
        int binaryFiles = 0;
        QString error;
        QVERIFY2(RepositoryExportCore::writeCombinedRepository(
                     source, QStringLiteral("krisCC"), QStringLiteral("main"),
                     destination, &textFiles, &binaryFiles, &error),
                 qPrintable(error));
        QCOMPARE(textFiles, 2);
        QCOMPARE(binaryFiles, 1);

        QFile output(destination);
        QVERIFY(output.open(QIODevice::ReadOnly));
        const QByteArray data = output.readAll();
        QVERIFY(data.contains("===== FILE: script.sh ====="));
        QVERIFY(data.contains("#!/bin/sh\necho ok"));
        QVERIFY(data.contains("===== FILE: .github/workflows/test.yml ====="));
        QVERIFY(data.contains("name: test"));
        QVERIFY(data.contains("[[BINARY FILE OMITTED"));
    }
};

QTEST_APPLESS_MAIN(RepositoryExportCoreTest)
#include "test_repository_export_core.moc"
