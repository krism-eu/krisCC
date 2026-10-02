#include "SystemBackend.h"
#include "SystemBackendRuntime.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <type_traits>

static_assert(std::is_abstract_v<SystemBackend>);
static_assert(!std::is_abstract_v<SystemBackendRuntime>);

// Qt invokes the inherited metaobject: it must still dispatch to the override.
class DispatchProbe final : public SystemBackend
{
public:
    DispatchProbe() : SystemBackend(nullptr) {}
    QStringList calls;
    void refreshServiceStates() override { calls << "services"; }
    void refreshUefiEntries() override { calls << "uefi"; }
    void refreshUefiEntriesPrivileged() override { calls << "privileged"; }
    void refreshGrubEntries() override { calls << "grub"; }
    bool startService(const QString &unit) override { calls << "start:" + unit; return true; }
    bool stopService(const QString &unit) override { calls << "stop:" + unit; return true; }
    bool restartService(const QString &unit) override { calls << "restart:" + unit; return true; }
};

class SystemBackendContractTest : public QObject
{
    Q_OBJECT
private slots:
    void inheritedMetaobjectDispatch()
    {
        DispatchProbe probe;
        for (const char *method : {"refreshServiceStates", "refreshUefiEntries",
                                   "refreshUefiEntriesPrivileged", "refreshGrubEntries"})
            QVERIFY(QMetaObject::invokeMethod(&probe, method, Qt::DirectConnection));
        for (const char *method : {"startService", "stopService", "restartService"}) {
            bool result = false;
            QVERIFY(QMetaObject::invokeMethod(&probe, method, Qt::DirectConnection,
                    Q_RETURN_ARG(bool, result), Q_ARG(QString, QStringLiteral("example.service"))));
            QVERIFY(result);
        }
        QCOMPARE(probe.calls, QStringList({"services", "uefi", "privileged", "grub",
                 "start:example.service", "stop:example.service", "restart:example.service"}));
        QTimer::singleShot(0, &probe, &SystemBackend::refreshServiceStates);
        QTRY_COMPARE(probe.calls.size(), 8);
        QCOMPARE(probe.calls.last(), QStringLiteral("services"));
    }

    void independentBootCompletion()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto script = [&directory](const QString &name, const QByteArray &body) {
            QFile file(directory.filePath(name));
            if (!file.open(QIODevice::WriteOnly))
                return false;
            if (file.write(body) != body.size())
                return false;
            file.close();
            return file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                       | QFileDevice::ExeOwner);
        };
        // GRUB waits on a file so the assertion does not depend on timing/sleeps.
        QVERIFY(script("efibootmgr", "#!/bin/sh\nexit 1\n"));
        QVERIFY(script("grubby", "#!/bin/sh\nwhile [ ! -f \"$0.release\" ]; do /bin/sleep 0.02; done\nexit 0\n"));
        const QByteArray previousPath = qgetenv("PATH");
        struct RestorePath {
            QByteArray value;
            ~RestorePath() { qputenv("PATH", value); }
        } restorePath{previousPath};
        qputenv("PATH", directory.path().toUtf8());
        SystemBackendRuntime backend(nullptr);
        QSignalSpy notifications(&backend, &SystemBackend::bootEntriesChanged);
        QVERIFY(QMetaObject::invokeMethod(&backend, "refreshUefiEntries", Qt::DirectConnection));
        QVERIFY(QMetaObject::invokeMethod(&backend, "refreshGrubEntries", Qt::DirectConnection));
        QVERIFY(backend.uefiEntriesBusy());
        QVERIFY(backend.grubEntriesBusy());
        QTRY_VERIFY(!backend.uefiEntriesBusy());
        QVERIFY(backend.grubEntriesBusy());
        const QString uefiError = backend.uefiEntriesError();
        QVERIFY(!uefiError.isEmpty());
        QVERIFY(backend.grubEntriesError().isEmpty());
        QFile release(directory.filePath("grubby.release"));
        QVERIFY(release.open(QIODevice::WriteOnly));
        release.close();
        QTRY_VERIFY(!backend.grubEntriesBusy());
        QCOMPARE(backend.uefiEntriesError(), uefiError);
        QVERIFY(notifications.count() >= 4);
        QVERIFY(backend.metaObject()->indexOfProperty("uefiEntriesBusy") >= 0);
        QVERIFY(backend.metaObject()->indexOfProperty("grubEntriesBusy") >= 0);
        QCOMPARE(backend.metaObject()->indexOfProperty("bootEntriesBusy"), -1);
        QCOMPARE(backend.metaObject()->indexOfProperty("bootEntriesError"), -1);
    }
};

QTEST_GUILESS_MAIN(SystemBackendContractTest)
#include "test_system_backend_contract.moc"
