#include <QtTest>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "PackageInventoryCache.h"

namespace {
void writeFile(const QString &path, const QByteArray &data, QFileDevice::Permissions permissions = {})
{
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(file.errorString()));
    QCOMPARE(file.write(data), data.size());
    file.close();
    if (permissions != QFileDevice::Permissions{})
        QVERIFY(file.setPermissions(permissions));
}

int lineCount(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return 0;
    int count = 0;
    while (!file.atEnd()) {
        file.readLine();
        ++count;
    }
    return count;
}
}

class PackageInventoryTest final : public QObject
{
    Q_OBJECT
private slots:
    void cacheIsReusedAndInvalidated()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString owned = dir.filePath(QStringLiteral("owned.txt"));
        const QString persistent = dir.filePath(QStringLiteral("packages.list"));
        const QString counter = dir.filePath(QStringLiteral("counter"));
        const QString rpm = dir.filePath(QStringLiteral("rpm"));
        writeFile(owned, "bash\n");
        writeFile(persistent, "qt6-qtbase\n");
        writeFile(rpm, QStringLiteral("#!/usr/bin/bash\necho x >> '%1'\nprintf 'bash\\nqt6-qtbase\\n'\n").arg(counter).toUtf8(),
                  QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

        PackageInventoryCache::Config config;
        config.rpmProgram = rpm;
        config.ownedManifest = owned;
        config.persistentManifest = persistent;
        config.ttlMs = 60 * 1000;
        PackageInventoryCache cache(config);
        QSignalSpy spy(&cache, &PackageInventoryCache::refreshFinished);

        cache.ensureFresh();
        QVERIFY(spy.wait(3000));
        QVERIFY(spy.takeFirst().at(0).toBool());
        QVERIFY(cache.ready());
        QCOMPARE(cache.loadCount(), quint64(1));
        QCOMPARE(lineCount(counter), 1);
        QVERIFY(cache.installed().contains(QStringLiteral("bash")));
        QVERIFY(cache.owned().contains(QStringLiteral("bash")));
        QVERIFY(cache.persistent().contains(QStringLiteral("qt6-qtbase")));

        cache.ensureFresh();
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 1000);
        QVERIFY(spy.takeFirst().at(0).toBool());
        QCOMPARE(cache.loadCount(), quint64(1));
        QCOMPARE(lineCount(counter), 1);

        cache.invalidate();
        cache.ensureFresh();
        QVERIFY(spy.wait(3000));
        QVERIFY(spy.takeFirst().at(0).toBool());
        QCOMPARE(cache.loadCount(), quint64(2));
        QCOMPARE(lineCount(counter), 2);
    }

    void missingOwnedManifestRecovers()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString owned = dir.filePath(QStringLiteral("owned.txt"));
        const QString rpm = dir.filePath(QStringLiteral("rpm"));
        writeFile(rpm, "#!/usr/bin/bash\nprintf 'bash\\n'\n",
                  QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

        PackageInventoryCache::Config config;
        config.rpmProgram = rpm;
        config.ownedManifest = owned;
        config.persistentManifest = dir.filePath(QStringLiteral("missing-packages.list"));
        PackageInventoryCache cache(config);
        QSignalSpy spy(&cache, &PackageInventoryCache::refreshFinished);

        cache.ensureFresh();
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 1000);
        QVERIFY(!spy.takeFirst().at(0).toBool());
        QVERIFY(!cache.ready());
        QCOMPARE(cache.loadCount(), quint64(0));

        writeFile(owned, "bash\n");
        cache.ensureFresh(true);
        QVERIFY(spy.wait(3000));
        QVERIFY(spy.takeFirst().at(0).toBool());
        QVERIFY(cache.ready());
        QVERIFY(cache.owned().contains(QStringLiteral("bash")));
    }

    void concurrentRequestsShareOneRpmLoad()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString owned = dir.filePath(QStringLiteral("owned.txt"));
        const QString rpm = dir.filePath(QStringLiteral("rpm"));
        const QString counter = dir.filePath(QStringLiteral("counter"));
        writeFile(owned, "base\n");
        writeFile(rpm, QStringLiteral("#!/usr/bin/bash\necho x >> '%1'\nsleep 0.2\nprintf 'base\\n'\n").arg(counter).toUtf8(),
                  QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

        PackageInventoryCache::Config config;
        config.rpmProgram = rpm;
        config.ownedManifest = owned;
        config.persistentManifest = dir.filePath(QStringLiteral("none"));
        PackageInventoryCache cache(config);
        QSignalSpy spy(&cache, &PackageInventoryCache::refreshFinished);

        cache.ensureFresh();
        cache.ensureFresh();
        cache.ensureFresh();
        QVERIFY(spy.wait(3000));
        QVERIFY(spy.takeFirst().at(0).toBool());
        QCOMPARE(cache.loadCount(), quint64(1));
        QCOMPARE(lineCount(counter), 1);
    }

    void forcedRefreshPublishesMetadataAtomically()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString owned = dir.filePath(QStringLiteral("owned.txt"));
        const QString rpm = dir.filePath(QStringLiteral("rpm"));
        writeFile(owned, "base-a\n");
        writeFile(rpm, "#!/usr/bin/bash\nprintf 'base-a\\nbase-b\\n'\n",
                  QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

        PackageInventoryCache::Config config;
        config.rpmProgram = rpm;
        config.ownedManifest = owned;
        config.persistentManifest = dir.filePath(QStringLiteral("none"));
        PackageInventoryCache cache(config);
        QSignalSpy spy(&cache, &PackageInventoryCache::refreshFinished);

        cache.ensureFresh();
        QVERIFY(spy.wait(3000));
        QVERIFY(spy.takeFirst().at(0).toBool());
        QVERIFY(cache.owned().contains(QStringLiteral("base-a")));

        writeFile(owned, "base-b\n");
        cache.ensureFresh(true);
        QVERIFY(spy.wait(3000));
        QVERIFY(spy.takeFirst().at(0).toBool());
        QVERIFY(!cache.owned().contains(QStringLiteral("base-a")));
        QVERIFY(cache.owned().contains(QStringLiteral("base-b")));
    }

    void failedRefreshNeverPublishesEmptyInventory()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString owned = dir.filePath(QStringLiteral("owned.txt"));
        const QString rpm = dir.filePath(QStringLiteral("rpm"));
        writeFile(owned, "base\n");
        writeFile(rpm, "#!/usr/bin/bash\nprintf 'base\\n'\n",
                  QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

        PackageInventoryCache::Config config;
        config.rpmProgram = rpm;
        config.ownedManifest = owned;
        config.persistentManifest = dir.filePath(QStringLiteral("none"));
        PackageInventoryCache cache(config);
        QSignalSpy spy(&cache, &PackageInventoryCache::refreshFinished);

        cache.ensureFresh();
        QVERIFY(spy.wait(3000));
        QVERIFY(spy.takeFirst().at(0).toBool());
        QVERIFY(cache.installed().contains(QStringLiteral("base")));

        writeFile(rpm, "#!/usr/bin/bash\nprintf 'partial\\n'\nexit 1\n",
                  QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        cache.ensureFresh(true);
        QVERIFY(spy.wait(3000));
        QVERIFY(!spy.takeFirst().at(0).toBool());
        QVERIFY(!cache.ready());
        QVERIFY(cache.installed().contains(QStringLiteral("base")));
        QVERIFY(!cache.installed().contains(QStringLiteral("partial")));
    }
};

QTEST_GUILESS_MAIN(PackageInventoryTest)
#include "test_package_inventory.moc"
