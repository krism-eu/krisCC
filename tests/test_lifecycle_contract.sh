#!/usr/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RUNTIME_H="$ROOT/src/SystemBackendRuntime.h"
LIFECYCLE_CPP="$ROOT/src/SystemBackendRuntimeLifecycle.cpp"
SERVICES_CPP="$ROOT/src/SystemBackendRuntimeServices.cpp"
MAIN_QML="$ROOT/qml/Main.qml"

grep -Fq 'Q_PROPERTY(bool mutationRunning READ mutationRunning NOTIFY mutationRunningChanged)' "$RUNTIME_H"
grep -Fq 'Q_INVOKABLE void requestFirmwareReboot();' "$RUNTIME_H"
grep -Fq 'm_polkit && m_polkit->running()' "$LIFECYCLE_CPP"
grep -Fq 'm_systemdJobCoordinator && m_systemdJobCoordinator->busy()' "$LIFECYCLE_CPP"
grep -Fq 'if (mutationRunning())' "$LIFECYCLE_CPP"
grep -Fq 'SystemBackendRuntime::requestFirmwareReboot()' "$LIFECYCLE_CPP"
grep -Fq 'SystemBackend::requestFirmwareReboot();' "$LIFECYCLE_CPP"
grep -Fq 'Riavvio nel firmware rimandato' "$LIFECYCLE_CPP"
grep -Fq 'emit mutationRunningChanged();' "$SERVICES_CPP"
grep -Fq 'SystemBackend.mutationRunning' "$MAIN_QML"
grep -Fq 'ServiceManagerBackend.busy' "$MAIN_QML"
grep -Fq 'if (SystemBackend.backupBusy)' "$MAIN_QML"
grep -Fq 'closeAfterBackupCancel && !SystemBackend.backupBusy' "$MAIN_QML"
