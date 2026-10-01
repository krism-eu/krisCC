#include "SystemdJobCoordinator.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QSet>

namespace {
const QString kService = QStringLiteral("org.freedesktop.systemd1");
const QString kManagerPath = QStringLiteral("/org/freedesktop/systemd1");
const QString kManagerInterface = QStringLiteral("org.freedesktop.systemd1.Manager");
}

SystemdJobCoordinator::SystemdJobCoordinator(QObject *parent)
    : QObject(parent)
{
    const QDBusConnection bus = QDBusConnection::systemBus();
    m_owner = currentOwner();
    m_serviceWatcher = new QDBusServiceWatcher(
        kService, bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(m_serviceWatcher, &QDBusServiceWatcher::serviceOwnerChanged,
            this, &SystemdJobCoordinator::onOwnerChanged);

    bus.connect(kService, kManagerPath, kManagerInterface,
                QStringLiteral("JobRemoved"), this,
                SLOT(onJobRemoved(uint,QDBusObjectPath,QString,QString)));

    m_timeout.setSingleShot(true);
    m_timeout.setInterval(60 * 1000);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (m_active)
            finish(false, tr("Tempo massimo superato attendendo il job systemd."));
    });
}

SystemdJobCoordinator::~SystemdJobCoordinator() = default;

QString SystemdJobCoordinator::currentOwner() const
{
    QDBusConnectionInterface *interface = QDBusConnection::systemBus().interface();
    if (!interface)
        return {};
    const QDBusReply<QString> reply = interface->serviceOwner(kService);
    return reply.isValid() ? reply.value() : QString();
}

bool SystemdJobCoordinator::start(const QString &unit, const QString &action)
{
    static const QSet<QString> actions = {
        QStringLiteral("start"), QStringLiteral("stop"), QStringLiteral("restart")
    };
    if (m_active || unit.isEmpty() || !actions.contains(action))
        return false;

    const QString owner = currentOwner();
    if (owner.isEmpty())
        return false;
    if (owner != m_owner) {
        m_owner = owner;
        m_subscribed = false;
    }

    m_unit = unit;
    m_action = action;
    m_jobPath.clear();
    m_earlyResults.clear();
    m_active = true;
    m_timeout.start();
    ensureSubscribedAndSend();
    return true;
}

void SystemdJobCoordinator::ensureSubscribedAndSend()
{
    if (!m_active)
        return;
    if (m_subscribed) {
        sendRequest();
        return;
    }

    QDBusInterface manager(kService, kManagerPath, kManagerInterface,
                           QDBusConnection::systemBus());
    if (!manager.isValid()) {
        finish(false, tr("systemd non è disponibile sul bus di sistema."));
        return;
    }

    auto *watcher = new QDBusPendingCallWatcher(
        manager.asyncCall(QStringLiteral("Subscribe")), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher *call) {
        const QDBusPendingReply<> reply(*call);
        call->deleteLater();
        if (!m_active)
            return;
        if (reply.isError()) {
            finish(false, tr("Impossibile sottoscrivere gli eventi systemd: %1")
                              .arg(reply.error().message()));
            return;
        }
        m_subscribed = true;
        sendRequest();
    });
}

void SystemdJobCoordinator::sendRequest()
{
    if (!m_active)
        return;

    QDBusInterface manager(kService, kManagerPath, kManagerInterface,
                           QDBusConnection::systemBus());
    if (!manager.isValid()) {
        finish(false, tr("systemd non è disponibile sul bus di sistema."));
        return;
    }
    manager.setInteractiveAuthorizationAllowed(true);

    QString method;
    if (m_action == QStringLiteral("start"))
        method = QStringLiteral("StartUnit");
    else if (m_action == QStringLiteral("stop"))
        method = QStringLiteral("StopUnit");
    else
        method = QStringLiteral("RestartUnit");

    auto *watcher = new QDBusPendingCallWatcher(
        manager.asyncCall(method, m_unit, QStringLiteral("replace")), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher *call) {
        const QDBusPendingReply<QDBusObjectPath> reply(*call);
        call->deleteLater();
        if (!m_active)
            return;
        if (reply.isError()) {
            finish(false, reply.error().message());
            return;
        }

        m_jobPath = reply.value().path();
        const auto early = m_earlyResults.constFind(m_jobPath);
        if (early != m_earlyResults.cend()) {
            const QString result = early.value();
            finish(result == QStringLiteral("done"),
                   result == QStringLiteral("done")
                       ? tr("Job systemd completato.")
                       : tr("Job systemd terminato con esito: %1").arg(result));
        }
    });
}

void SystemdJobCoordinator::onJobRemoved(uint, const QDBusObjectPath &job,
                                         const QString &unit, const QString &result)
{
    if (!m_active || unit != m_unit)
        return;

    const QString path = job.path();
    if (m_jobPath.isEmpty()) {
        m_earlyResults.insert(path, result);
        return;
    }
    if (path != m_jobPath)
        return;

    finish(result == QStringLiteral("done"),
           result == QStringLiteral("done")
               ? tr("Job systemd completato.")
               : tr("Job systemd terminato con esito: %1").arg(result));
}

void SystemdJobCoordinator::onOwnerChanged(const QString &service,
                                           const QString &, const QString &newOwner)
{
    if (service != kService)
        return;
    if (newOwner == m_owner)
        return;

    m_owner = newOwner;
    m_subscribed = false;
    if (m_active)
        finish(false, tr("Il manager systemd è stato riavviato durante l'operazione."));
    emit stateMayHaveChanged();
}

void SystemdJobCoordinator::finish(bool success, const QString &message)
{
    if (!m_active)
        return;

    const QString unit = m_unit;
    const QString action = m_action;
    m_timeout.stop();
    m_active = false;
    m_unit.clear();
    m_action.clear();
    m_jobPath.clear();
    m_earlyResults.clear();

    // State is final before signals: direct slots are free to start another job.
    emit completed(unit, action, success, message);
    emit stateMayHaveChanged();
}
