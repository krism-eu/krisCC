#include "SystemBackendRuntime.h"

#include "PolkitHelper.h"
#include "ProcessRunner.h"
#include "SystemdJobCoordinator.h"

#include <QProcess>

namespace {
void stopBootReader(const QPointer<QProcess> &process)
{
    if (!process || process->state() == QProcess::NotRunning)
        return;
    process->terminate();
    if (!process->waitForFinished(1000)) {
        process->kill();
        process->waitForFinished(1000);
    }
}
}

SystemBackendRuntime::SystemBackendRuntime(PolkitHelper *polkit, QObject *parent)
    : SystemBackend(polkit, parent)
{
    if (m_polkit) {
        connect(m_polkit, &PolkitHelper::runningChanged,
                this, &SystemBackendRuntime::mutationRunningChanged);
    }
}

bool SystemBackendRuntime::mutationRunning() const
{
    return (m_polkit && m_polkit->running())
        || m_bootSelectionRunning
        || (m_systemdJobCoordinator && m_systemdJobCoordinator->busy());
}

SystemBackendRuntime::~SystemBackendRuntime()
{
    stopBootReader(m_uefiProcess);
    stopBootReader(m_grubProcess);
}

void SystemBackendRuntime::requestReboot()
{
    if (mutationRunning()) {
        emit rebootFinished(false,
                            tr("Riavvio rimandato: è ancora in corso una modifica di sistema o un'operazione amministrativa."));
        return;
    }
    SystemBackend::requestReboot();
}

void SystemBackendRuntime::requestFirmwareReboot()
{
    // SetRebootToFirmwareSetup changes persistent reboot intent in logind. Do not
    // set it until the same lifecycle gate used by ordinary reboot is clear.
    if (mutationRunning()) {
        emit rebootFinished(false,
                            tr("Riavvio nel firmware rimandato: è ancora in corso una modifica di sistema o un'operazione amministrativa."));
        return;
    }
    SystemBackend::requestFirmwareReboot();
}
