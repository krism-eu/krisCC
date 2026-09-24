#!/usr/bin/env bash
set -euo pipefail

grep -Fq 'json) exec /usr/bin/timeout --signal=TERM --kill-after=3s 30s /usr/bin/bootc status --format json --format-version=1 ;;' src/bootc-status.sh
if grep -Fq '"$@"' src/bootc-status.sh; then
  echo "ERROR: bootc-status accepts arbitrary arguments" >&2
  exit 1
fi
if grep -Fq 'auth_admin_keep' data/org.kriscc.controlcenter.policy; then
  echo "ERROR: retained Polkit authorization is forbidden" >&2
  exit 1
fi
if grep -RniE '/usr/bin/(bootc|dnf5|efibootmgr|grub2-reboot)|PolkitHelper' qml; then
  echo "ERROR: privileged implementation detail leaked into QML" >&2
  exit 1
fi

grep -Fq 'ProcessRunner' src/CustomActionsBackend.cpp
grep -Fq 'setStandardInputFile(QProcess::nullDevice())' src/ProcessRunner.cpp
grep -Fq 'setStandardInputFile(QProcess::nullDevice())' src/AdminHelper.cpp
grep -Fq 'AdminPolicy::resolve' src/AdminHelper.cpp
grep -Fq 'AdminPolicy::resolve' src/PolkitHelper.cpp
if grep -Fq 'rpmdb.sqlite' src/PackageSearch.cpp; then
  echo "ERROR: PackageSearch hardcodes an RPM database path" >&2
  exit 1
fi

if grep -Fq 'Layout.preferredHeight: contentHeight' qml/modules/SoftwareModule.qml; then
  echo "ERROR: Software list virtualization regressed" >&2
  exit 1
fi

dnf5 config-manager --help >/dev/null
dnf5 config-manager enable --help >/dev/null
dnf5 config-manager disable --help >/dev/null
dnf5 config-manager addrepo --help | grep -F -- '--from-repofile' >/dev/null
bootc status --help | grep -F -- '--format-version' >/dev/null

test "$(rpmspec -q --qf '%{NAME}\n' packaging/krisCC.spec | head -n1)" = krisCC
echo "e2e-readonly: OK"