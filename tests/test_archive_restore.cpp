#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

#include <sys/stat.h>
#include <unistd.h>

#include "ArchiveRestoreEngine.h"

class ArchiveRestoreTest final : public QObject
{
    Q_OBJECT

    static QString archiveDirectory(const QString &source, const QString &output,
                                    const QStringList &members = {QStringLiteral(".")})
    {
        QProcess tar;
        QStringList args{QStringLiteral("-czf"), output, QStringLiteral("-C"), source};
        args.append(members);
        tar.start(QStringLiteral("/usr/bin/tar"), args);
        if (!tar.waitForFinished(10000) || tar.exitStatus() != QProcess::NormalExit || tar.exitCode() != 0)
            return {};
        return output;
    }

    static QByteArray readAll(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return file.readAll();
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
        QVERIFY(::symlink("../file", QFile::encodeName(source.filePath(QStringLiteral("sub/link"))).constData()) == 0);

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
        QVERIFY(::symlink("../outside", QFile::encodeName(source.filePath(QStringLiteral("link"))).constData()) == 0);
        const QString archive = archiveDirectory(source.path(), work.filePath(QStringLiteral("fixture.tar.gz")));
        QVERIFY(!archive.isEmpty());
        const ArchiveOperationResult result = ArchiveRestoreEngine::validate(archive);
        QVERIFY(!result.success);
    }

    void absoluteSymlinkIsPreservedButNeverTraversed()
    {
        QTemporaryDir source;
        QTemporaryDir work;
        QTemporaryDir destination;
        QVERIFY(source.isValid() && work.isValid() && destination.isValid());
        QVERIFY(::symlink("/tmp/kriscc-external-target",
                          QFile::encodeName(source.filePath(QStringLiteral("absolute-link"))).constData()) == 0);
        const QString archive = archiveDirectory(source.path(), work.filePath(QStringLiteral("fixture.tar.gz")));
        QVERIFY(!archive.isEmpty());
        const ArchiveOperationResult valid = ArchiveRestoreEngine::validate(archive);
        QVERIFY2(valid.success, qPrintable(valid.message));
        const ArchiveOperationResult restored = ArchiveRestoreEngine::restore(archive, destination.path());
        QVERIFY2(restored.success, qPrintable(restored.message));
        const QFileInfo link(destination.filePath(QStringLiteral("absolute-link")));
        QVERIFY(link.isSymLink());
        QCOMPARE(link.symLinkTarget(), QStringLiteral("/tmp/kriscc-external-target"));
    }

    void preexistingDestinationSymlinkCannotEscape()
    {
        QTemporaryDir source;
        QTemporaryDir work;
        QTemporaryDir destination;
        QTemporaryDir outside;
        QVERIFY(source.isValid() && work.isValid() && destination.isValid() && outside.isValid());
        QVERIFY(QDir(source.path()).mkpath(QStringLiteral("link")));
        QFile payload(source.filePath(QStringLiteral("link/victim")));
        QVERIFY(payload.open(QIODevice::WriteOnly));
        QCOMPARE(payload.write("new-data"), qint64(8));
        payload.close();

        const QString outsideVictim = outside.filePath(QStringLiteral("victim"));
        QFile external(outsideVictim);
        QVERIFY(external.open(QIODevice::WriteOnly));
        QCOMPARE(external.write("keep-me"), qint64(7));
        external.close();

        QVERIFY(::symlink(QFile::encodeName(outside.path()).constData(),
                          QFile::encodeName(destination.filePath(QStringLiteral("link"))).constData()) == 0);
        const QString archive = archiveDirectory(source.path(), work.filePath(QStringLiteral("fixture.tar.gz")));
        QVERIFY(!archive.isEmpty());
        const ArchiveOperationResult restored = ArchiveRestoreEngine::restore(archive, destination.path());
        QVERIFY(!restored.success);
        QCOMPARE(readAll(outsideVictim), QByteArray("keep-me"));
    }

    void internalHardLinkIsAcceptedAndRestored()
    {
        QTemporaryDir source;
        QTemporaryDir work;
        QTemporaryDir destination;
        QVERIFY(source.isValid() && work.isValid() && destination.isValid());
        const QString original = source.filePath(QStringLiteral("original"));
        QFile file(original);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("same-inode"), qint64(10));
        file.close();
        QVERIFY(::link(QFile::encodeName(original).constData(),
                       QFile::encodeName(source.filePath(QStringLiteral("hard"))).constData()) == 0);

        const QString archive = archiveDirectory(source.path(), work.filePath(QStringLiteral("fixture.tar.gz")));
        QVERIFY(!archive.isEmpty());
        const ArchiveOperationResult valid = ArchiveRestoreEngine::validate(archive);
        QVERIFY2(valid.success, qPrintable(valid.message));
        const ArchiveOperationResult restored = ArchiveRestoreEngine::restore(archive, destination.path());
        QVERIFY2(restored.success, qPrintable(restored.message));
        QCOMPARE(readAll(destination.filePath(QStringLiteral("hard"))), QByteArray("same-inode"));

        struct stat first {};
        struct stat second {};
        QVERIFY(::stat(QFile::encodeName(destination.filePath(QStringLiteral("original"))).constData(), &first) == 0);
        QVERIFY(::stat(QFile::encodeName(destination.filePath(QStringLiteral("hard"))).constData(), &second) == 0);
        QCOMPARE(first.st_dev, second.st_dev);
        QCOMPARE(first.st_ino, second.st_ino);
    }

    void duplicateMemberIsRejected()
    {
        QTemporaryDir source;
        QTemporaryDir work;
        QVERIFY(source.isValid() && work.isValid());
        QFile file(source.filePath(QStringLiteral("duplicate")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("x"), qint64(1));
        file.close();

        const QString archive = archiveDirectory(
            source.path(), work.filePath(QStringLiteral("fixture.tar.gz")),
            {QStringLiteral("duplicate"), QStringLiteral("duplicate")});
        QVERIFY(!archive.isEmpty());
        const ArchiveOperationResult result = ArchiveRestoreEngine::validate(archive);
        QVERIFY(!result.success);
        QVERIFY(result.message.contains(QStringLiteral("duplicato"), Qt::CaseInsensitive)
                || result.message.contains(QStringLiteral("ambiguo"), Qt::CaseInsensitive));
    }

    void missingHardLinkTargetIsRejected()
    {
        QTemporaryDir work;
        QVERIFY(work.isValid());
        const QString archive = work.filePath(QStringLiteral("fixture.tar.gz"));
        const QString script = QStringLiteral(
            "import tarfile\n"
            "p=%1\n"
            "with tarfile.open(p, 'w:gz') as t:\n"
            "    i=tarfile.TarInfo('hard')\n"
            "    i.type=tarfile.LNKTYPE\n"
            "    i.linkname='missing'\n"
            "    t.addfile(i)\n").arg(QStringLiteral("r'%1'").arg(archive));
        QProcess python;
        python.start(QStringLiteral("/usr/bin/python3"), {QStringLiteral("-c"), script});
        QVERIFY(python.waitForFinished(10000));
        QCOMPARE(python.exitCode(), 0);

        const ArchiveOperationResult result = ArchiveRestoreEngine::validate(archive);
        QVERIFY(!result.success);
        QVERIFY(result.message.contains(QStringLiteral("assente"), Qt::CaseInsensitive));
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
