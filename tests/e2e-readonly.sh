#!/usr/bin/env bash
set -euo pipefail

fail() {
  echo "ERROR: $*" >&2
  exit 1
}

echo "Checking release identity..."
grep -Fxq 'Version:        0.7.0' packaging/krisCC.spec
grep -Fxq 'Release:        7%{?dist}' packaging/krisCC.spec
grep -Fq 'set(KRISCC_RELEASE 7)' CMakeLists.txt
grep -Fq 'KRISCC_VERSION="${PROJECT_VERSION}-${KRISCC_RELEASE}"' CMakeLists.txt

echo "Checking privileged architecture..."
grep -Fq 'm_polkit->execute(QStringLiteral("/usr/libexec/kriscc/admin")' src/RkBackend.cpp
grep -Fq 'm_polkit->execute(QStringLiteral("/usr/libexec/kriscc/admin")' src/BootcBackend.cpp
grep -Fq 'm_polkit->execute(QStringLiteral("/usr/libexec/kriscc/admin")' src/SoftwareBackend.cpp
grep -Fq 'm_polkit->execute(QStringLiteral("/usr/libexec/kriscc/admin")' src/SystemBackend.cpp
! grep -Fq 'm_polkit->execute(QStringLiteral("/usr/bin/rk")' src/RkBackend.cpp
grep -Fq 'buildAdminCommand' src/AdminHelper.cpp
grep -Fq 'runPrivilegedCommand' src/AdminHelper.cpp
grep -Fq 'setStandardInputFile(QProcess::nullDevice())' src/PrivilegedProcessRunner.cpp
grep -Fq 'kill(-pid, SIGTERM)' src/PrivilegedProcessRunner.cpp
grep -Fq 'kill(-pid, SIGKILL)' src/PrivilegedProcessRunner.cpp
grep -Fq 'return 124' src/PrivilegedProcessRunner.cpp
! grep -Eq '/usr/bin/(bash|sh)' src/AdminPolicy.cpp

echo "Checking shared validation and user-script stdin..."
grep -Fq 'Validators::packageName' src/RkBackend.cpp
grep -Fq 'Validators::repositoryId' src/SoftwareBackend.cpp
grep -Fq 'Validators::packageName' src/AdminPolicy.cpp
grep -Fq 'setStandardInputFile(QProcess::nullDevice())' src/CustomActionsBackend.cpp

echo "Checking structured rk/bootc contracts..."
grep -Fq 'QStringLiteral("status"), QStringLiteral("--json")' src/RkBackend.cpp
grep -Fq 'object.value(QStringLiteral("schema")).toInt(-1) != 1' src/RkBackend.cpp
grep -Fq 'QStringLiteral("--format-version=1")' src/BootcBackend.cpp
grep -Fq 'QStringLiteral("--from-downloaded")' src/AdminPolicy.cpp
grep -Fq 'QStringLiteral("--apply")' src/AdminPolicy.cpp

echo "Checking UI/memory contracts..."
! grep -R -F 'preferredHeight: contentHeight' qml/modules
! grep -R -F 'PageIntro {' qml/modules
! grep -Fq 'Novità repository' qml/modules/SoftwareModule.qml
grep -Fq 'searchModel.truncated' qml/modules/SoftwareModule.qml
grep -Fq 'SystemBackend.launchFlatpak(modelData[1])' qml/modules/FlatpakModule.qml
grep -Fq 'SystemBackend.topMemoryProcesses' qml/modules/DashboardModule.qml
grep -Fq 'qsTr("Firewall")' qml/modules/DashboardModule.qml
grep -Fq 'aboutDialog.open()' qml/Main.qml
! grep -Fq 'bootTechnicalDetails' qml/modules/SystemModule.qml

echo "Checking backup and Podman contracts..."
grep -Fq 'QStringLiteral(".var/app/*/cache")' src/SystemBackend.cpp
grep -Fq 'umask(0077)' src/SystemBackend.cpp
grep -Fq 'Q_INVOKABLE bool removeSnapshot' src/SystemBackend.h
grep -Fq 'mode == QStringLiteral("remove")' src/UtilityBackend.cpp
grep -Fq 'QStringLiteral("rm"), name' src/UtilityBackend.cpp
grep -Fq 'typeof item.Size === "object"' qml/modules/PodmanModule.qml

echo "Checking mandatory DNF5 machine-readable behavior..."
dnf5 list --installed --json >/tmp/kriscc-dnf-installed.json
python3 - <<'PY'
import json
with open("/tmp/kriscc-dnf-installed.json", encoding="utf-8") as f:
    data = json.load(f)
if not isinstance(data, dict):
    raise SystemExit("dnf5 installed JSON top-level contract is not an object")
PY

echo "Checking DNF5 config-manager subcommands used by krisCC..."
dnf5 config-manager --help >/tmp/kriscc-config-manager-help.txt
for subcommand in enable disable addrepo; do
  grep -Eq "(^|[[:space:]])${subcommand}([[:space:]]|$)" /tmp/kriscc-config-manager-help.txt     || fail "dnf5 config-manager does not advertise ${subcommand}"
done
dnf5 config-manager addrepo --help >/tmp/kriscc-addrepo-help.txt
grep -Fq -- '--from-repofile' /tmp/kriscc-addrepo-help.txt   || fail "dnf5 config-manager addrepo lacks --from-repofile"

echo "Checking repository-backed DNF5 query shape when metadata is available..."
if dnf5 repo list --all --json >/tmp/kriscc-repos.json 2>/tmp/kriscc-repos.err; then
  python3 - <<'PY'
import json
with open("/tmp/kriscc-repos.json", encoding="utf-8") as f:
    data = json.load(f)
if not isinstance(data, dict):
    raise SystemExit("dnf5 repo JSON top-level contract is not an object")
PY
  if dnf5 repoquery --available --latest-limit=1       --queryformat $'%{name}\t%{summary}\t%{evr}\t%{repoid}\t%{arch}\t%{downloadsize}\t%{installsize}\n'       'bash*' >/tmp/kriscc-repoquery.txt 2>/tmp/kriscc-repoquery.err; then
    if grep -q '^bash' /tmp/kriscc-repoquery.txt; then
      awk -F '\t' 'NR == 1 { exit (NF == 7 ? 0 : 1) }' /tmp/kriscc-repoquery.txt         || fail "dnf5 repoquery contract no longer has seven fields"
    fi
  else
    echo "WARNING: optional repoquery probe unavailable"
  fi
else
  echo "WARNING: repository metadata unavailable; repository-backed probes skipped"
fi

echo "Checking BootC CLI flags used by the fixed admin policy..."
bootc upgrade --help >/tmp/kriscc-bootc-upgrade-help.txt
for flag in --check --download-only --from-downloaded --apply; do
  grep -Fq -- "$flag" /tmp/kriscc-bootc-upgrade-help.txt     || fail "installed bootc does not support ${flag}"
done
bootc status --help >/tmp/kriscc-bootc-status-help.txt
grep -Fq -- '--format-version' /tmp/kriscc-bootc-status-help.txt   || fail "installed bootc does not support --format-version"

echo "e2e-readonly: PASS"
