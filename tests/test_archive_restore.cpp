#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

#include "ArchiveRestoreEngine.h"

class ArchiveRestoreTest final : public QObject
{
    Q_OBJECT

    static QString archiveDirectory(const QString &source, const QString &output)
    {
        QProcess tar;
        tar.start(QStringLiteral("/usr/bin/tar"),
                  {QStringLiteral("-czf"), output, QStringLiteral("-C"), source, QStringLiteral(".")});
        if (!tar.waitForFinished(10000) || tar.exitStatus() != QProcess::NormalExit || tar.exitCode() != 0)
            return {};
        return output;
    }

private slots:
    void internalRelativeLinkIsAccepted()
    {
        QTemporaryDir source;
        QTemporaryDir work;
        QTemporaryDir destination;
        QVERIFY(source.isValid() && work.isValid() && destination.isValid());
        QVERIFY(QDir(source.path()).mkpath(QStringLiteral("sub")));
        QFile file(source.filePath(QStringLiteral("file")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("payload"), qint64(7));
        file.close();
        QVERIFY(QFile::link(QStringLiteral("../file"), source.filePath(QStringLiteral("sub/link"))));

        const QString archive = archiveDirectory(source.path(), work.filePath(QStringLiteral("fixture.tar.gz")));
        QVERIFY(!archive.isEmpty());
        const ArchiveOperationResult valid = ArchiveRestoreEngine::validate(archive);
        QVERIFY2(valid.success, qPrintable(valid.message));
        const ArchiveOperationResult restored = ArchiveRestoreEngine::restore(archive, destination.path());
        QVERIFY2(restored.success, qPrintable(restored.message));
        QVERIFY(QFileInfo(destination.filePath(QStringLiteral("sub/link"))).isSymLink());
    }

    void relativeTargetLeavingRootIsRejected()
    {
        QTemporaryDir source;
        QTemporaryDir work;
        QVERIFY(source.isValid() && work.isValid());
        QVERIFY(QFile::link(QStringLiteral("../outside"), source.filePath(QStringLiteral("link"))));
        const QString archive = archiveDirectory(source.path(), work.filePath(QStringLiteral("fixture.tar.gz")));
        QVERIFY(!archive.isEmpty());
        const ArchiveOperationResult result = ArchiveRestoreEngine::validate(archive);
        QVERIFY(!result.success);
    }

    void delimiterTextInRegularNameIsAccepted()
    {
        QTemporaryDir source;
        QTemporaryDir work;
        QVERIFY(source.isValid() && work.isValid());
        QFile file(source.filePath(QStringLiteral("name -> regular")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("ok"), qint64(2));
        file.close();
        const QString archive = archiveDirectory(source.path(), work.filePath(QStringLiteral("fixture.tar.gz")));
        QVERIFY(!archive.isEmpty());
        const ArchiveOperationResult result = ArchiveRestoreEngine::validate(archive);
        QVERIFY2(result.success, qPrintable(result.message));
    }
};

QTEST_APPLESS_MAIN(ArchiveRestoreTest)
#include "test_archive_restore.moc"
