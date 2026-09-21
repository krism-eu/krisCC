#!/usr/bin/env bash
set -euo pipefail

grep -Fq 'json) exec /usr/bin/bootc status --format json --format-version=1 ;;' src/bootc-status.sh
! grep -Fq '"$@"' src/bootc-status.sh
! grep -Fq 'auth_admin_keep' data/org.kriscc.controlcenter.policy
! grep -RniE '/usr/bin/(bootc|dnf5|efibootmgr|grub2-reboot)|PolkitHelper' qml

grep -Fq 'setStandardInputFile(QProcess::nullDevice())' src/CustomActionsBackend.cpp
grep -Fq 'setStandardInputFile(QProcess::nullDevice())' src/AdminHelper.cpp
grep -Fq 'AdminPolicy::resolve' src/AdminHelper.cpp
grep -Fq 'AdminPolicy::resolve' src/PolkitHelper.cpp
grep -Fq 'QStringLiteral("/usr/share/rpm/rpmdb.sqlite")' src/PackageSearch.cpp
! grep -Fq 'Layout.preferredHeight: contentHeight' qml/modules/SoftwareModule.qml
! grep -Fq 'Layout.preferredHeight: contentHeight' qml/modules/FlatpakModule.qml
! grep -Fq 'Novità repository' qml/modules/SoftwareModule.qml

dnf5 config-manager --help >/dev/null
dnf5 config-manager enable --help >/dev/null
dnf5 config-manager disable --help >/dev/null
dnf5 config-manager addrepo --help | grep -F -- '--from-repofile' >/dev/null
bootc status --help | grep -F -- '--format-version' >/dev/null

test "$(rpmspec -q --qf '%{NAME}\n' packaging/krisCC.spec | head -n1)" = krisCC
echo "e2e-readonly: OK"
