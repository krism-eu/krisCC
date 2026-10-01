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

    // A forced external shutdown cannot wait for the interactive coordinator.
    // Keep a still-active workspace instead of deleting it under a running
    // helper; the base destructor then terminates the tracked process group.
    if (m_backupRunner && m_backupRunner->running() && m_backupWorkspace)
        m_backupWorkspace->setAutoRemove(false);
}

void SystemBackendRuntime::requestReboot()
{
    if (m_backupBusy) {
        emit rebootFinished(false,
                            tr("Riavvio rimandato: attendere il completamento o annullare backup/verifica/ripristino."));
        return;
    }
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
    if (m_backupBusy) {
        emit rebootFinished(false,
                            tr("Riavvio nel firmware rimandato: attendere il completamento o annullare backup/verifica/ripristino."));
        return;
    }
    if (mutationRunning()) {
        emit rebootFinished(false,
                            tr("Riavvio nel firmware rimandato: è ancora in corso una modifica di sistema o un'operazione amministrativa."));
        return;
    }
    SystemBackend::requestFirmwareReboot();
}
