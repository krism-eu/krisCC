#!/usr/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RUNTIME_H="$ROOT/src/SystemBackendRuntime.h"
LIFECYCLE_CPP="$ROOT/src/SystemBackendRuntimeLifecycle.cpp"
SERVICES_CPP="$ROOT/src/SystemBackendRuntimeServices.cpp"
MAIN_QML="$ROOT/qml/Main.qml"
SYSTEM_QML="$ROOT/qml/modules/SystemModule.qml"

grep -Fq 'Q_PROPERTY(bool mutationRunning READ mutationRunning NOTIFY mutationRunningChanged)' "$RUNTIME_H"
grep -Fq 'Q_INVOKABLE void requestFirmwareReboot() override;' "$RUNTIME_H"
grep -Fq 'Q_INVOKABLE virtual void requestFirmwareReboot();' "$ROOT/src/SystemBackend.h"
grep -Fq 'm_polkit && m_polkit->running()' "$LIFECYCLE_CPP"
grep -Fq 'm_systemdJobCoordinator && m_systemdJobCoordinator->busy()' "$LIFECYCLE_CPP"
grep -Fq 'if (mutationRunning())' "$LIFECYCLE_CPP"
grep -Fq 'SystemBackendRuntime::requestFirmwareReboot()' "$LIFECYCLE_CPP"
grep -Fq 'SystemBackend::requestFirmwareReboot();' "$LIFECYCLE_CPP"
grep -Fq 'Riavvio nel firmware rimandato' "$LIFECYCLE_CPP"
grep -Fq 'emit mutationRunningChanged();' "$SERVICES_CPP"
grep -Fq 'SystemBackend.mutationRunning' "$MAIN_QML"
grep -Fq 'ServiceManagerBackend.busy' "$MAIN_QML"


# F2: reboot uses the application mutation gate
grep -Fq 'appMutationRunning: root.mutationActive()' "$MAIN_QML"
grep -Fq 'property bool appMutationRunning: false' "$SYSTEM_QML"
grep -Fq 'function requestRebootSafely(firmware)' "$SYSTEM_QML"
grep -Fq 'root.requestRebootSafely(false)' "$SYSTEM_QML"
grep -Fq 'root.requestRebootSafely(true)' "$SYSTEM_QML"

test "$(grep -Fc 'SystemBackend.requestReboot()' "$SYSTEM_QML")" -eq 1
test "$(grep -Fc 'SystemBackend.requestFirmwareReboot()' "$SYSTEM_QML")" -eq 1

# Internal personal backup is retired.
if grep -Fq 'SystemBackend.backupBusy' "$MAIN_QML"; then
    exit 1
fi
if grep -Fq 'cancelSnapshot' "$MAIN_QML"; then
    exit 1
fi
grep -Fq 'if (root.mutationActive())' "$MAIN_QML"
