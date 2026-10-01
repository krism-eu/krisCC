#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "MaintenanceTrash.h"

class MaintenanceTrashTest final : public QObject
{
    Q_OBJECT
private slots:
    void dataIsRemovedBeforeMetadata()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QVERIFY(QDir(root.path()).mkpath(QStringLiteral("files")));
        QVERIFY(QDir(root.path()).mkpath(QStringLiteral("info")));

        QFile data(root.filePath(QStringLiteral("files/item")));
        QVERIFY(data.open(QIODevice::WriteOnly));
        QCOMPARE(data.write("payload"), qint64(7));
        data.close();
        QFile metadata(root.filePath(QStringLiteral("info/item.trashinfo")));
        QVERIFY(metadata.open(QIODevice::WriteOnly));
        QCOMPARE(metadata.write("[Trash Info]\n"), qint64(13));
        metadata.close();

        const auto result = KrisccMaintenance::cleanTrashRoot(root.path());
        QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join('\n')));
        QVERIFY(!QFileInfo::exists(data.fileName()));
        QVERIFY(!QFileInfo::exists(metadata.fileName()));
    }

    void unsafeDataContainerPreservesMetadata()
    {
        QTemporaryDir root;
        QTemporaryDir outside;
        QVERIFY(root.isValid() && outside.isValid());
        QVERIFY(QDir(root.path()).mkpath(QStringLiteral("info")));
        QFile outsideData(outside.filePath(QStringLiteral("item")));
        QVERIFY(outsideData.open(QIODevice::WriteOnly));
        QCOMPARE(outsideData.write("keep"), qint64(4));
        outsideData.close();
        QVERIFY(QFile::link(outside.path(), root.filePath(QStringLiteral("files"))));

        QFile metadata(root.filePath(QStringLiteral("info/item.trashinfo")));
        QVERIFY(metadata.open(QIODevice::WriteOnly));
        QCOMPARE(metadata.write("[Trash Info]\n"), qint64(13));
        metadata.close();

        const auto result = KrisccMaintenance::cleanTrashRoot(root.path());
        QVERIFY(!result.errors.isEmpty());
        QVERIFY(QFileInfo::exists(metadata.fileName()));
        QVERIFY(QFileInfo::exists(outsideData.fileName()));
    }

    void trueOrphanMetadataCanBeRemoved()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QVERIFY(QDir(root.path()).mkpath(QStringLiteral("files")));
        QVERIFY(QDir(root.path()).mkpath(QStringLiteral("info")));
        QFile metadata(root.filePath(QStringLiteral("info/orphan.trashinfo")));
        QVERIFY(metadata.open(QIODevice::WriteOnly));
        metadata.write("[Trash Info]\n");
        metadata.close();

        const auto result = KrisccMaintenance::cleanTrashRoot(root.path());
        QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join('\n')));
        QVERIFY(!QFileInfo::exists(metadata.fileName()));
    }
};

QTEST_APPLESS_MAIN(MaintenanceTrashTest)
#include "test_maintenance_trash.moc"
