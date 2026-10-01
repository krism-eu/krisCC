#!/usr/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RUNTIME_H="$ROOT/src/SystemBackendRuntime.h"
RUNTIME_BOOT="$ROOT/src/SystemBackendRuntimeBoot.cpp"
SYSTEM_QML="$ROOT/qml/modules/SystemModule.qml"

grep -Fq 'Q_PROPERTY(bool uefiEntriesBusy READ uefiEntriesBusy NOTIFY bootEntriesChanged)' "$RUNTIME_H"
grep -Fq 'Q_PROPERTY(bool grubEntriesBusy READ grubEntriesBusy NOTIFY bootEntriesChanged)' "$RUNTIME_H"
grep -Fq 'Q_PROPERTY(QString uefiEntriesError READ uefiEntriesError NOTIFY bootEntriesChanged)' "$RUNTIME_H"
grep -Fq 'Q_PROPERTY(QString grubEntriesError READ grubEntriesError NOTIFY bootEntriesChanged)' "$RUNTIME_H"
grep -Fq 'QPointer<QProcess> m_uefiProcess' "$RUNTIME_H"
grep -Fq 'QPointer<QProcess> m_grubProcess' "$RUNTIME_H"
grep -Fq 'm_uefiRequestGeneration' "$RUNTIME_BOOT"
grep -Fq 'm_grubRequestGeneration' "$RUNTIME_BOOT"
grep -Fq '!SystemBackend.uefiEntriesBusy' "$SYSTEM_QML"
grep -Fq '!SystemBackend.grubEntriesBusy' "$SYSTEM_QML"
grep -Fq 'SystemBackend.uefiEntriesError' "$SYSTEM_QML"
grep -Fq 'SystemBackend.grubEntriesError' "$SYSTEM_QML"
if grep -Fq 'SystemBackend.bootEntriesBusy' "$SYSTEM_QML"; then
    echo 'SystemModule still couples UEFI/GRUB controls through aggregate busy state' >&2
    exit 1
fi
if grep -Fq 'SystemBackend.bootEntriesError' "$SYSTEM_QML"; then
    echo 'SystemModule still couples UEFI/GRUB errors through aggregate error state' >&2
    exit 1
fi
