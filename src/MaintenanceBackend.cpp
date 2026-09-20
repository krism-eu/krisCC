#include "MaintenanceBackend.h"

#include "PolkitHelper.h"

MaintenanceBackend::MaintenanceBackend(PolkitHelper *polkit, QObject *parent)
    : QObject(parent)
    , m_polkit(polkit)
{
    if (!m_polkit)
        return;

    connect(m_polkit, &PolkitHelper::runningChanged, this, &MaintenanceBackend::stateChanged);
    connect(m_polkit, &PolkitHelper::finished, this,
            [this](bool success, const QString &output) {
        if (!m_ownedOperation)
            return;

        m_ownedOperation = false;
        m_running = false;
        m_resultState = success ? QStringLiteral("success") : QStringLiteral("error");
        m_output = output;
        emit stateChanged();
        emit finished(success, output);
    });
}

bool MaintenanceBackend::available() const
{
    return m_polkit && !m_polkit->running() && !m_running;
}

bool MaintenanceBackend::cleanTrash(const QString &scope)
{
    if (!available())
        return false;

    QString argument;
    if (scope == QStringLiteral("home"))
        argument = QStringLiteral("trash-home");
    else if (scope == QStringLiteral("system"))
        argument = QStringLiteral("trash-system");
    else if (scope == QStringLiteral("all"))
        argument = QStringLiteral("trash-all");
    else
        return false;

    m_scope = scope;
    m_output.clear();
    m_resultState = QStringLiteral("running");
    m_ownedOperation = true;
    m_running = true;
    emit stateChanged();

    m_polkit->execute(QStringLiteral("/usr/libexec/kriscc/maintenance"), {argument});
    return true;
}
