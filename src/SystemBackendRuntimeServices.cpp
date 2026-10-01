#include "SystemBackendRuntime.h"

#include "SystemdJobCoordinator.h"

#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QSet>
#include <QTimer>

namespace {
const QSet<QString> &managedServices()
{
    static const QSet<QString> services = {
        QStringLiteral("NetworkManager.service"),
        QStringLiteral("cups.service"),
        QStringLiteral("bluetooth.service"),
        QStringLiteral("firewalld.service"),
        QStringLiteral("cockpit.socket")
    };
    return services;
}

bool noSuchUnitError(const QDBusError &error)
{
    return error.name().contains(QStringLiteral("NoSuchUnit"), Qt::CaseInsensitive)
        || error.name().contains(QStringLiteral("NoSuchFile"), Qt::CaseInsensitive);
}
}

SystemdJobCoordinator *SystemBackendRuntime::systemdJobs()
{
    if (m_systemdJobCoordinator)
        return m_systemdJobCoordinator;

    m_systemdJobCoordinator = new SystemdJobCoordinator(this);
    connect(m_systemdJobCoordinator, &SystemdJobCoordinator::completed, this,
            [this](const QString &unit, const QString &action,
                   bool success, const QString &message) {
        const QString title = action == QStringLiteral("start")
            ? (success ? tr("Servizio avviato") : tr("Avvio servizio non riuscito"))
            : action == QStringLiteral("stop")
                ? (success ? tr("Servizio arrestato") : tr("Arresto servizio non riuscito"))
                : (success ? tr("Servizio riavviato") : tr("Riavvio servizio non riuscito"));
        notify(title, success ? unit : message);
        refreshServiceStates();
    });
    connect(m_systemdJobCoordinator, &SystemdJobCoordinator::stateMayHaveChanged, this,
            [this] {
        QTimer::singleShot(150, this, &SystemBackendRuntime::refreshServiceStates);
    });
    return m_systemdJobCoordinator;
}

void SystemBackendRuntime::refreshServiceStates()
{
    const quint64 generation = ++m_serviceRefreshGeneration;
    for (const QString &service : managedServices())
        m_serviceStates.insert(service, QStringLiteral("loading"));
    m_serviceStates.insert(QStringLiteral("wifi"), QStringLiteral("loading"));
    emit serviceStatesChanged();

    QDBusInterface manager(QStringLiteral("org.freedesktop.systemd1"),
                           QStringLiteral("/org/freedesktop/systemd1"),
                           QStringLiteral("org.freedesktop.systemd1.Manager"),
                           QDBusConnection::systemBus());
    if (!manager.isValid()) {
        for (const QString &service : managedServices())
            m_serviceStates.insert(service, QStringLiteral("error"));
        emit serviceStatesChanged();
    } else {
        for (const QString &service : managedServices()) {
            auto *unitWatcher = new QDBusPendingCallWatcher(
                manager.asyncCall(QStringLiteral("GetUnit"), service), this);
            connect(unitWatcher, &QDBusPendingCallWatcher::finished, this,
                    [this, service, generation](QDBusPendingCallWatcher *call) {
                const QDBusPendingReply<QDBusObjectPath> unitReply(*call);
                call->deleteLater();
                if (generation != m_serviceRefreshGeneration)
                    return;

                if (unitReply.isError()) {
                    if (!noSuchUnitError(unitReply.error())) {
                        m_serviceStates.insert(service, QStringLiteral("error"));
                        emit serviceStatesChanged();
                        return;
                    }

                    QDBusInterface managerAgain(QStringLiteral("org.freedesktop.systemd1"),
                                                QStringLiteral("/org/freedesktop/systemd1"),
                                                QStringLiteral("org.freedesktop.systemd1.Manager"),
                                                QDBusConnection::systemBus());
                    if (!managerAgain.isValid()) {
                        m_serviceStates.insert(service, QStringLiteral("error"));
                        emit serviceStatesChanged();
                        return;
                    }
                    auto *fileWatcher = new QDBusPendingCallWatcher(
                        managerAgain.asyncCall(QStringLiteral("GetUnitFileState"), service), this);
                    connect(fileWatcher, &QDBusPendingCallWatcher::finished, this,
                            [this, service, generation](QDBusPendingCallWatcher *fileCall) {
                        const QDBusPendingReply<QString> fileReply(*fileCall);
                        fileCall->deleteLater();
                        if (generation != m_serviceRefreshGeneration)
                            return;
                        if (!fileReply.isError())
                            m_serviceStates.insert(service, QStringLiteral("not-loaded"));
                        else if (noSuchUnitError(fileReply.error()))
                            m_serviceStates.insert(service, QStringLiteral("not-installed"));
                        else
                            m_serviceStates.insert(service, QStringLiteral("error"));
                        emit serviceStatesChanged();
                    });
                    return;
                }

                QDBusInterface properties(QStringLiteral("org.freedesktop.systemd1"),
                                          unitReply.value().path(),
                                          QStringLiteral("org.freedesktop.DBus.Properties"),
                                          QDBusConnection::systemBus());
                if (!properties.isValid()) {
                    m_serviceStates.insert(service, QStringLiteral("error"));
                    emit serviceStatesChanged();
                    return;
                }
                auto *stateWatcher = new QDBusPendingCallWatcher(
                    properties.asyncCall(QStringLiteral("Get"),
                                         QStringLiteral("org.freedesktop.systemd1.Unit"),
                                         QStringLiteral("ActiveState")), this);
                connect(stateWatcher, &QDBusPendingCallWatcher::finished, this,
                        [this, service, generation](QDBusPendingCallWatcher *stateCall) {
                    const QDBusPendingReply<QDBusVariant> stateReply(*stateCall);
                    stateCall->deleteLater();
                    if (generation != m_serviceRefreshGeneration)
                        return;
                    m_serviceStates.insert(service,
                        stateReply.isError() ? QStringLiteral("error")
                                             : stateReply.value().variant().toString());
                    emit serviceStatesChanged();
                });
            });
        }
    }

    QDBusInterface nmProperties(QStringLiteral("org.freedesktop.NetworkManager"),
                                QStringLiteral("/org/freedesktop/NetworkManager"),
                                QStringLiteral("org.freedesktop.DBus.Properties"),
                                QDBusConnection::systemBus());
    if (!nmProperties.isValid()) {
        m_serviceStates.insert(QStringLiteral("wifi"), QStringLiteral("not-installed"));
        emit serviceStatesChanged();
    } else {
        auto *wifiWatcher = new QDBusPendingCallWatcher(
            nmProperties.asyncCall(QStringLiteral("Get"),
                                   QStringLiteral("org.freedesktop.NetworkManager"),
                                   QStringLiteral("WirelessEnabled")), this);
        connect(wifiWatcher, &QDBusPendingCallWatcher::finished, this,
                [this, generation](QDBusPendingCallWatcher *call) {
            const QDBusPendingReply<QDBusVariant> reply(*call);
            call->deleteLater();
            if (generation != m_serviceRefreshGeneration)
                return;
            m_serviceStates.insert(QStringLiteral("wifi"),
                reply.isError() ? QStringLiteral("error")
                                : (reply.value().variant().toBool()
                                       ? QStringLiteral("active")
                                       : QStringLiteral("inactive")));
            emit serviceStatesChanged();
        });
    }
}

bool SystemBackendRuntime::startSystemdJob(const QString &service, const QString &action)
{
    if (!managedServices().contains(service)) {
        notify(tr("Operazione servizio non riuscita"),
               tr("Servizio non autorizzato dal Control Center: %1").arg(service));
        return false;
    }
    SystemdJobCoordinator *coordinator = systemdJobs();
    if (coordinator->busy()) {
        notify(tr("Operazione servizio non riuscita"), tr("Un altro job systemd è ancora in corso."));
        return false;
    }
    if (!coordinator->start(service, action)) {
        notify(tr("Operazione servizio non riuscita"), tr("Impossibile avviare o sottoscrivere il job systemd."));
        return false;
    }
    return true;
}

bool SystemBackendRuntime::startService(const QString &service)
{
    if (service == QStringLiteral("wifi"))
        return setWifiRadio(true, false);
    return startSystemdJob(service, QStringLiteral("start"));
}

bool SystemBackendRuntime::stopService(const QString &service)
{
    if (service == QStringLiteral("wifi"))
        return setWifiRadio(false, false);
    return startSystemdJob(service, QStringLiteral("stop"));
}

bool SystemBackendRuntime::restartService(const QString &service)
{
    if (service == QStringLiteral("wifi"))
        return setWifiRadio(false, true);
    return startSystemdJob(service, QStringLiteral("restart"));
}
