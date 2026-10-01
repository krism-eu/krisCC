#include "SystemBackendRuntime.h"

#include "PolkitHelper.h"
#include "ProcessRunner.h"

SystemBackendRuntime::~SystemBackendRuntime()
{
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
    if ((m_polkit && m_polkit->running()) || m_bootSelectionRunning) {
        emit rebootFinished(false,
                            tr("Riavvio rimandato: è ancora in corso un'operazione amministrativa."));
        return;
    }
    SystemBackend::requestReboot();
}
