#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTimer>
#include <QWindow>
#include <QtQml/qqml.h>

#include "BootcBackend.h"
#include "CustomActionsBackend.h"
#include "InstanceController.h"
#include "PackageSearch.h"
#include "PolkitHelper.h"
#include "RkBackend.h"
#include "MaintenanceBackend.h"
#include "SoftwareBackend.h"
#include "SystemBackend.h"
#include "UtilityBackend.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("krisCC"));
    QCoreApplication::setApplicationName(QStringLiteral("krisCC"));
    QCoreApplication::setApplicationVersion(QStringLiteral(KRISCC_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("krisCC control center"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption backgroundOption(
        QStringList{QStringLiteral("background")},
        QStringLiteral("Start the single krisCC instance without opening the main window."));
    parser.addOption(backgroundOption);
    parser.process(app);

    const QString serviceName = QStringLiteral("org.kriscc.ControlCenter");
    const QString objectPath = QStringLiteral("/org/kriscc/ControlCenter");
    const QString interfaceName = QStringLiteral("org.kriscc.ControlCenter");
    QDBusConnection sessionBus = QDBusConnection::sessionBus();
    bool ownsSingleInstanceService = false;
    bool singleInstanceReady = false;
    InstanceController instanceController;

    if (sessionBus.isConnected()) {
        ownsSingleInstanceService = sessionBus.registerService(serviceName);
        if (!ownsSingleInstanceService) {
            QDBusInterface existing(serviceName, objectPath, interfaceName, sessionBus);
            if (existing.isValid()) {
                existing.call(QDBus::NoBlock, QStringLiteral("show"));
                return 0;
            }
        } else {
            singleInstanceReady = sessionBus.registerObject(objectPath, &instanceController,
                                                             QDBusConnection::ExportScriptableSlots);
            if (!singleInstanceReady) {
                sessionBus.unregisterService(serviceName);
                ownsSingleInstanceService = false;
            }
        }
    }

    // Never leave an unreachable hidden process when the session bus or activation object is unavailable.
    const bool startHidden = parser.isSet(backgroundOption) && singleInstanceReady;

    qmlRegisterType<PackageSearch>("org.kriscc", 1, 0, "PackageSearch");
    qmlRegisterType<UtilityBackend>("org.kriscc", 1, 0, "UtilityBackend");

    PolkitHelper polkitHelper;
    BootcBackend bootcBackend(&polkitHelper);
    RkBackend rkBackend(&polkitHelper);
    MaintenanceBackend maintenanceBackend;
    SoftwareBackend softwareBackend(&polkitHelper);
    SystemBackend systemBackend(&polkitHelper);
    CustomActionsBackend customActionsBackend;

    QObject::connect(&rkBackend, &RkBackend::operationFinished, &bootcBackend,
                     [&bootcBackend](bool, const QString &) {
        bootcBackend.refreshPackages();
    });

    QObject::connect(&polkitHelper, &PolkitHelper::finished, &systemBackend,
                     [&systemBackend](bool success, const QString &output) {
        const QString title = success
            ? QCoreApplication::translate("main", "Operazione completata")
            : QCoreApplication::translate("main", "Operazione non riuscita");
        systemBackend.notify(title, output.left(500));
    });

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("BootcBackend"), &bootcBackend);
    engine.rootContext()->setContextProperty(QStringLiteral("RkBackend"), &rkBackend);
    engine.rootContext()->setContextProperty(QStringLiteral("MaintenanceBackend"), &maintenanceBackend);
    engine.rootContext()->setContextProperty(QStringLiteral("SoftwareBackend"), &softwareBackend);
    engine.rootContext()->setContextProperty(QStringLiteral("SystemBackend"), &systemBackend);
    engine.rootContext()->setContextProperty(QStringLiteral("CustomActionsBackend"), &customActionsBackend);
    engine.rootContext()->setContextProperty(QStringLiteral("KrisccStartHidden"), startHidden);
    const bool smokeTest = qEnvironmentVariableIsSet("KRISCC_SMOKE_TEST");
    engine.rootContext()->setContextProperty(QStringLiteral("KrisccSmokeTest"), smokeTest);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.loadFromModule(QStringLiteral("org.kriscc"), QStringLiteral("Main"));

    if (!engine.rootObjects().isEmpty()) {
        if (auto *window = qobject_cast<QWindow *>(engine.rootObjects().constFirst()))
            instanceController.setWindow(window);
    }

    if (smokeTest) {
        systemBackend.setResourceMonitoringEnabled(true);
        if (systemBackend.memoryTotalMiB() <= 0 || systemBackend.memoryUsedMiB() < 0
            || systemBackend.memoryUsedMiB() > systemBackend.memoryTotalMiB()) {
            qCritical("Smoke test: invalid RAM sample from procfs");
            return 2;
        }
    }
    if (smokeTest)
        QTimer::singleShot(2200, &app, &QCoreApplication::quit);

    return app.exec();
}
