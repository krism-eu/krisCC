#include <QtTest>

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "BackupSafety.h"

class BackupSafetyTest final : public QObject
{
    Q_OBJECT
private slots:
    void acceptedNamesMatchActions()
    {
        QVERIFY(BackupSafety::validBackupName(QStringLiteral("config-20261001-070000.tar.gz")));
        QVERIFY(BackupSafety::validBackupName(QStringLiteral("home-20261001-070000.tar.gz")));
        QVERIFY(!BackupSafety::validBackupName(QStringLiteral("config-manuale.tar.gz")));
        QVERIFY(!BackupSafety::validBackupName(QStringLiteral("home-test.tar.gz")));
        QVERIFY(!BackupSafety::validBackupName(QStringLiteral("home-20261001-070000.tar.gz.partial")));
    }

    void linkedArchiveIsNotListedOrDeleted()
    {
        QTemporaryDir backupRoot;
        QTemporaryDir outsideRoot;
        QVERIFY(backupRoot.isValid());
        QVERIFY(outsideRoot.isValid());

        const QString target = outsideRoot.filePath(QStringLiteral("document.txt"));
        QFile targetFile(target);
        QVERIFY(targetFile.open(QIODevice::WriteOnly));
        QCOMPARE(targetFile.write("keep-me"), qint64(7));
        targetFile.close();

        const QString linkPath = backupRoot.filePath(QStringLiteral("home-20261001-070000.tar.gz"));
        QVERIFY(QFile::link(target, linkPath));
        QVERIFY(QFileInfo(linkPath).isSymLink());

        QString listError;
        const QVariantList listed = BackupSafety::listBackups(backupRoot.path(), &listError);
        QVERIFY2(listError.isEmpty(), qPrintable(listError));
        QVERIFY(listed.isEmpty());

        QString deleteError;
        QVERIFY(!BackupSafety::removeBackup(backupRoot.path(), linkPath, nullptr, &deleteError));
        QVERIFY(!deleteError.isEmpty());

        QVERIFY(targetFile.open(QIODevice::ReadOnly));
        QCOMPARE(targetFile.readAll(), QByteArray("keep-me"));
    }

    void regularArchiveIsRemoved()
    {
        QTemporaryDir backupRoot;
        QVERIFY(backupRoot.isValid());
        const QString path = backupRoot.filePath(QStringLiteral("config-20261001-070000.tar.gz"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("archive"), qint64(7));
        file.close();

        QString name;
        QString error;
        QVERIFY2(BackupSafety::removeBackup(backupRoot.path(), path, &name, &error), qPrintable(error));
        QCOMPARE(name, QStringLiteral("config-20261001-070000.tar.gz"));
        QVERIFY(!QFileInfo::exists(path));
    }
};

QTEST_APPLESS_MAIN(BackupSafetyTest)
#include "test_backup_safety.moc"
