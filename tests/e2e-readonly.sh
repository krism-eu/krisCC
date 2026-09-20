#!/usr/bin/env bash
set -euo pipefail

# Static/read-only anti-regression checks plus lightweight command probes.
# This is not a security boundary or a substitute for runtime/integration testing.

if grep -Eq '"--search"|QStringLiteral\("--search"\)' src/PackageSearch.cpp; then
  echo "ERROR: PackageSearch still contains unsupported dnf5 repoquery --search" >&2
  exit 1
fi

# DNF5 repoquery does not translate a literal backslash+t for us. The C++
# queryformat must contain escaped C++ tabs (\t), not a double-escaped \\t
# sequence that reaches DNF5 as visible text.
if grep -Fq '%{name}\\\\t%{summary}' src/PackageSearch.cpp; then
  echo "ERROR: PackageSearch queryformat contains literal backslash-t separators" >&2
  exit 1
fi

if grep -Eq '<allow_(any|inactive|active)>auth_admin_keep</allow_' data/org.kriscc.controlcenter.policy; then
  echo "ERROR: Polkit policy must not retain admin authorization" >&2
  exit 1
fi

grep -q 'org.kriscc.controlcenter.bootc.status' data/org.kriscc.controlcenter.policy
grep -A6 'org.kriscc.controlcenter.bootc.status' data/org.kriscc.controlcenter.policy \
  | grep -q '<allow_active>yes</allow_active>'
grep -A8 'org.kriscc.controlcenter.bootc.status' data/org.kriscc.controlcenter.policy \
  | grep -q '/usr/libexec/kriscc/bootc-status'
grep -Fq 'json) exec /usr/bin/bootc status --format json --format-version=1 ;;' src/bootc-status.sh
grep -Fq 'humanreadable) exec /usr/bin/bootc status --format humanreadable ;;' src/bootc-status.sh
if grep -Fq '"$@"' src/bootc-status.sh; then
  echo "ERROR: bootc status wrapper must not pass arbitrary arguments" >&2
  exit 1
fi

# KrisOS supports a single deployment: rollback must not be offered or
# privileged. Repository management is allowed only through the constrained
# DNF5 config-manager path; rk remains the final policy gate for persistent RPMs.
if grep -RniE 'bootc[^\n]*rollback|"rollback"|Rollback \+ apply|Prepara rollback' \
    qml/modules/SystemModule.qml qml/modules/RecoveryModule.qml \
    src/PolkitHelper.cpp data/org.kriscc.controlcenter.policy; then
  echo "ERROR: unsupported BootC rollback remains exposed" >&2
  exit 1
fi
grep -q 'QStringLiteral("/usr/libexec/kriscc/admin")' src/PolkitHelper.cpp
grep -q 'QStringLiteral("repo-enable")' src/PolkitHelper.cpp
grep -q 'QStringLiteral("repo-disable")' src/PolkitHelper.cpp
grep -q 'QStringLiteral("repo-add")' src/PolkitHelper.cpp
grep -q 'isSafeRepositoryId' src/PolkitHelper.cpp
grep -q 'isSafeRepositoryUrl' src/PolkitHelper.cpp
grep -q 'org.kriscc.controlcenter.admin' data/org.kriscc.controlcenter.policy
grep -q '/usr/libexec/kriscc/admin' data/org.kriscc.controlcenter.policy
grep -q 'execProgram("/usr/bin/dnf5"' src/AdminHelper.cpp
grep -q 'Aggiungi repository' qml/modules/SoftwareModule.qml
grep -q 'url.scheme() == QStringLiteral("https")' src/PolkitHelper.cpp
grep -q 'url.userInfo().isEmpty()' src/PolkitHelper.cpp
if grep -q 'url.scheme() == QStringLiteral("http")' src/PolkitHelper.cpp; then
  echo "ERROR: repository URLs must be HTTPS-only" >&2
  exit 1
fi
grep -q 'Repository non aggiunto: usa un URL HTTPS valido' src/SoftwareBackend.cpp
if grep -q 'auth_admin_keep' data/org.kriscc.controlcenter.policy; then
  echo "ERROR: repository authorization must not be retained" >&2
  exit 1
fi

# RPM preview must be the same policy path as the actual rk transaction.
grep -q 'QStringLiteral("/usr/bin/rk")' src/UtilityBackend.cpp
grep -q 'QStringLiteral("plan")' src/UtilityBackend.cpp
if grep -nE 'QStringLiteral\("/usr/bin/dnf5"\).*QStringLiteral\("install"\)|--assumeno' src/UtilityBackend.cpp; then
  echo "ERROR: RPM preview bypasses rk policy" >&2
  exit 1
fi

# Package discovery follows the repositories currently enabled in DNF.
# Persistent installation still goes through rk, which may reject a package or
# repository that is outside the KrisOS policy. Installed inventory stays local.
if grep -q 'QStringLiteral("--repo=fedora,updates")' src/PackageSearch.cpp; then
  echo "ERROR: package discovery is still hard-coded to fedora,updates" >&2
  exit 1
fi
if grep -q 'id != QStringLiteral("fedora") && id != QStringLiteral("updates")' src/SoftwareBackend.cpp; then
  echo "ERROR: repository UI still hides configured repositories" >&2
  exit 1
fi
grep -q 'QStringLiteral("repoquery"), QStringLiteral("--available")' src/PackageSearch.cpp
grep -q 'QStringLiteral("--latest-limit=1")' src/PackageSearch.cpp
grep -q 'args << QStringLiteral("list") << filter << QStringLiteral("--json")' src/PackageSearch.cpp
grep -Fq "const QString key = name + QLatin1Char('\\x1f') + arch;" src/PackageSearch.cpp
grep -q 'm_installedFilter' src/PackageSearch.cpp
if grep -q 'visibleForFilter' qml/modules/SoftwareModule.qml; then
  echo "ERROR: installed RPM filtering still happens in QML delegates" >&2
  exit 1
fi
if grep -q 'Installing dependencies:\|Transaction Summary:\|Total size of inbound packages' qml/modules/SoftwareModule.qml; then
  echo "ERROR: locale-sensitive rk plan parser remains" >&2
  exit 1
fi

# Flatpak management is deliberately per-user. Inventory, remotes and mutations
# must all use the same installation scope so the UI never shows system refs it
# cannot modify.
grep -q 'QStringLiteral("list"), QStringLiteral("--user"), QStringLiteral("--app")' src/UtilityBackend.cpp
grep -q 'QStringLiteral("remotes"), QStringLiteral("--user")' src/UtilityBackend.cpp
grep -q 'QStringLiteral("search"), QStringLiteral("--user")' src/UtilityBackend.cpp
grep -q 'QStringLiteral("install"), QStringLiteral("--user"), QStringLiteral("--noninteractive")' src/UtilityBackend.cpp
grep -q 'QStringLiteral("--assumeyes")' src/UtilityBackend.cpp
grep -q 'mode == QStringLiteral("update-all")' src/UtilityBackend.cpp
grep -q 'mode == QStringLiteral("update")' src/UtilityBackend.cpp
grep -q 'QStringLiteral("update"), QStringLiteral("--user"), QStringLiteral("--noninteractive"), QStringLiteral("--assumeyes")' src/UtilityBackend.cpp
grep -q 'text: qsTr("Aggiorna tutto")' qml/modules/FlatpakModule.qml
grep -q 'text: qsTr("Aggiorna")' qml/modules/FlatpakModule.qml
grep -q 'selectedRemote' src/UtilityBackend.cpp
grep -Fq 'modelData[5]' qml/modules/FlatpakModule.qml
if grep -q 'currentIndex: root.mode' qml/modules/FlatpakModule.qml qml/modules/PodmanModule.qml; then
  echo "ERROR: tab currentIndex is still bound back to mode" >&2
  exit 1
fi

# BootC mutations are semantic backend actions; QML never constructs privileged argv.
grep -q 'BootcBackend.checkUpgrade()' qml/modules/SystemModule.qml
grep -q 'BootcBackend.downloadUpgrade()' qml/modules/SystemModule.qml
grep -q 'BootcBackend.prepareUpgrade()' qml/modules/SystemModule.qml
grep -q 'BootcBackend.applyDownloaded()' qml/modules/SystemModule.qml
grep -q 'text: qsTr("Controlla immagine")' qml/modules/SystemModule.qml
grep -q 'root.hasStagedDeployment()' qml/modules/SystemModule.qml
grep -q 'BootcBackend.operationLines' qml/modules/SystemModule.qml
grep -q 'BootcBackend.refreshStatus()' qml/modules/SystemModule.qml
grep -q '/usr/libexec/kriscc/bootc-status humanreadable' src/UtilityBackend.cpp
grep -q 'QStringLiteral("downloadOnly")' src/BootcBackend.cpp
grep -q 'SystemBackend.requestReboot()' qml/modules/SystemModule.qml
grep -q 'm_polkit->execute(QStringLiteral("/usr/libexec/kriscc/admin")' src/BootcBackend.cpp
grep -q 'QStringLiteral("bootc-check")' src/BootcBackend.cpp
grep -q 'QStringLiteral("bootc-download")' src/BootcBackend.cpp
grep -q 'QStringLiteral("bootc-prepare")' src/BootcBackend.cpp
grep -q 'QStringLiteral("bootc-apply-downloaded")' src/BootcBackend.cpp
grep -q 'execProgram("/usr/bin/bootc"' src/AdminHelper.cpp
grep -q 'org.freedesktop.login1.Manager' src/SystemBackend.cpp
grep -q 'constexpr int kInteractiveTimeoutMs = 30 \* 60 \* 1000;' src/UtilityBackend.cpp
grep -q 'constexpr int kPodmanActionTimeoutMs = 5 \* 60 \* 1000;' src/UtilityBackend.cpp
if grep -Fq 'QStringLiteral("upgrade"), QStringLiteral("--apply")' src/BootcBackend.cpp; then
  echo "ERROR: BootC apply bypasses the staged download-only state" >&2
  exit 1
fi

if grep -q 'QStringLiteral("--json")' src/BootcBackend.cpp; then
  echo "ERROR: BootcBackend must use bootc status --format json" >&2
  exit 1
fi

grep -q 'QStringLiteral("--format")' src/BootcBackend.cpp
grep -q 'QStringLiteral("--format-version=1")' src/BootcBackend.cpp
grep -q 'QStringLiteral("json")' src/BootcBackend.cpp
grep -q 'QStringLiteral("/usr/bin/pkexec")' src/BootcBackend.cpp
grep -q 'imageStatus.value(QStringLiteral("image")).toObject()' src/BootcBackend.cpp
grep -q 'deployment.value(QStringLiteral("ostree")).toObject()' src/BootcBackend.cpp
grep -q 'jsonString(ostree, QStringLiteral("checksum"))' src/BootcBackend.cpp

# KrisOS paths are the only supported runtime contract on the current line.
grep -q '/usr/share/krisos/owned-packages.txt' src/PackageSearch.cpp
grep -q '/var/lib/krisos/packages.list' src/PackageSearch.cpp
grep -q '/var/lib/krisos/packages.list' src/BootcBackend.cpp
if grep -RniE 'raku-kris|using legacy compatibility path' src/PackageSearch.cpp src/BootcBackend.cpp; then
  echo "ERROR: obsolete Raku compatibility path remains" >&2
  exit 1
fi

# krisCC has one technical identity. Old experimental package/application names
# must not be shipped or advertised as compatibility aliases.
test -f packaging/krisCC.spec
test ! -e packaging/kcc.spec
test ! -e packaging/k-controlc.spec
test -f data/krisCC.desktop
test ! -e data/kcc.desktop
test ! -e data/k-controlc.desktop
test -f data/icons/hicolor/scalable/apps/krisCC.svg
test ! -e data/icons/hicolor/scalable/apps/kcc.svg
test ! -e data/icons/hicolor/scalable/apps/k-controlc.svg
test -f data/org.kriscc.controlcenter.policy
test ! -e data/org.kcontrolc.controlcenter.policy
test -f data/org.kriscc.KrisCC.metainfo.xml
test ! -e data/org.kcontrolc.KControlC.metainfo.xml

grep -q '^Name:[[:space:]]*krisCC$' packaging/krisCC.spec
grep -Fxq 'Version:        0.7.0' packaging/krisCC.spec
grep -Fxq 'Release:        3%{?dist}' packaging/krisCC.spec
if grep -Eq '^Provides:[[:space:]]*(kcc|k-controlc)([[:space:]=]|$)|^Obsoletes:[[:space:]]*(kcc|k-controlc)([[:space:]<=>]|$)' packaging/krisCC.spec; then
  echo "ERROR: krisCC must not provide or obsolete experimental legacy identities" >&2
  exit 1
fi

grep -q 'qt_add_executable(krisCC' CMakeLists.txt
grep -q 'URI org.kriscc' CMakeLists.txt
grep -q 'install(TARGETS krisCC' CMakeLists.txt
grep -q 'data/krisCC.desktop' CMakeLists.txt
grep -q 'data/icons/hicolor/scalable/apps/krisCC.svg' CMakeLists.txt
grep -q 'data/org.kriscc.controlcenter.policy' CMakeLists.txt
grep -q 'src/AdminHelper.cpp' CMakeLists.txt
grep -q 'src/bootc-status.sh' CMakeLists.txt
grep -Fq '%{_libexecdir}/kriscc/admin' packaging/krisCC.spec
grep -Fq '%{_libexecdir}/kriscc/bootc-status' packaging/krisCC.spec
grep -q 'data/org.kriscc.KrisCC.metainfo.xml' CMakeLists.txt
grep -q '^Name=krisCC$' data/krisCC.desktop
grep -q '^Exec=krisCC$' data/krisCC.desktop
grep -q '^Icon=krisCC$' data/krisCC.desktop
grep -q '<id>org.kriscc.KrisCC</id>' data/org.kriscc.KrisCC.metainfo.xml
grep -q '<name>krisCC</name>' data/org.kriscc.KrisCC.metainfo.xml
grep -q '<provides><binary>krisCC</binary></provides>' data/org.kriscc.KrisCC.metainfo.xml
grep -q '<vendor>krisCC</vendor>' data/org.kriscc.controlcenter.policy

grep -q 'qmlRegisterType<PackageSearch>("org.kriscc"' src/main.cpp
grep -q 'loadFromModule(QStringLiteral("org.kriscc")' src/main.cpp
grep -q 'import org.kriscc' qml/Main.qml
grep -q 'import org.kriscc' qml/modules/SoftwareModule.qml
if grep -R -n 'org\.kcontrolc' CMakeLists.txt src/main.cpp qml data packaging; then
  echo "ERROR: old org.kcontrolc application identity remains" >&2
  exit 1
fi

# No old product branding may leak into the UI. Historical backup directories
# are allowed only as non-destructive exclusions in SystemBackend.
if grep -R -nE 'K-ControlC|(^|[^[:alnum:]])KCC([^[:alnum:]]|$)' qml; then
  echo "ERROR: visible legacy K-ControlC/KCC branding remains in QML" >&2
  exit 1
fi
grep -q 'Informazioni rapide di sistema' src/SystemBackend.cpp
grep -q 'QStringLiteral("/krisCC Backups")' src/SystemBackend.cpp
grep -q 'QStringLiteral("KCC Backups")' src/SystemBackend.cpp
grep -q 'QStringLiteral("K-ControlC Backups")' src/SystemBackend.cpp
grep -q 'args << QStringLiteral("--exclude=./") + excluded' src/SystemBackend.cpp
grep -q 'QStringLiteral(".local/share/flatpak")' src/SystemBackend.cpp
grep -q 'QStringLiteral(".local/share/containers")' src/SystemBackend.cpp
grep -Fq '.var/app' qml/modules/RecoveryModule.qml

if grep -R -nE 'org\.raku|import raku\.cc|raku Control Center|raku Fedora' \
    CMakeLists.txt src/main.cpp qml data/krisCC.desktop packaging/krisCC.spec \
    data/org.kriscc.controlcenter.policy data/org.kriscc.KrisCC.metainfo.xml; then
  echo "ERROR: legacy Raku branding remains in application identity/metadata" >&2
  exit 1
fi

if grep -R -nE 'github\.com/krism-eu/(K-ControlC|KCC)([^[:alnum:]]|$)' \
    CMakeLists.txt src qml data packaging README.md INTEGRAZIONE.md .github; then
  echo "ERROR: old repository URL remains" >&2
  exit 1
fi

# Read-only external queries must have watchdogs so busy/searching cannot remain forever.
grep -q 'krisccTimedOut' src/PackageSearch.cpp
grep -q 'krisccTimedOut' src/SoftwareBackend.cpp
grep -q 'krisccTimedOut' src/BootcBackend.cpp
grep -q 'Tempo massimo superato' src/UtilityBackend.cpp

# Backend/UI state must not depend on translated presentation strings.
grep -q 'Q_PROPERTY(QString operationId' src/UtilityBackend.h
grep -q 'Q_PROPERTY(QString resultState' src/UtilityBackend.h
grep -q 'utilityBackend.operationId === "rpm.plan"' qml/modules/SoftwareModule.qml
grep -q 'utilityBackend.operationId !== "podman.list"' qml/modules/PodmanModule.qml
grep -q 'utilityBackend.operationId === "bookmark.health"' qml/modules/SystemModule.qml
if grep -R -nE 'utilityBackend\.title[[:space:]]*(===|!==)[[:space:]]*qsTr|utilityBackend\.output[[:space:]]*===[[:space:]]*qsTr|backupStatus\.indexOf\(qsTr' qml; then
  echo "ERROR: translated UI strings are still used as backend state" >&2
  exit 1
fi

# Backups are atomic and the UI supports explicit verification and restore.
grep -q 'QStringLiteral(".partial")' src/SystemBackend.cpp
grep -q 'exitCode == 0 || exitCode == 1' src/SystemBackend.cpp
grep -q 'Q_INVOKABLE bool cancelSnapshot' src/SystemBackend.h
grep -q 'Q_INVOKABLE bool verifySnapshot' src/SystemBackend.h
grep -q 'Q_INVOKABLE bool restoreSnapshot' src/SystemBackend.h
grep -q 'validateBackupPath' src/SystemBackend.cpp
grep -q 'QProcess::nullDevice()' src/SystemBackend.cpp
grep -q 'Impossibile avviare la verifica' src/SystemBackend.cpp
grep -q 'Impossibile avviare il ripristino' src/SystemBackend.cpp
grep -q 'mode == QStringLiteral("remove-unused")' src/UtilityBackend.cpp
grep -A4 'mode == QStringLiteral("remove-unused")' src/UtilityBackend.cpp | grep -q 'QStringLiteral("--user")'
grep -A4 'mode == QStringLiteral("remove-unused")' src/UtilityBackend.cpp | grep -q 'QStringLiteral("--unused")'
grep -q 'Q_PROPERTY(QString backupState' src/SystemBackend.h
grep -q 'OperationLog::append' src/SystemBackend.cpp
grep -q 'src/OperationLog.cpp src/OperationLog.h' CMakeLists.txt
grep -q 'Q_INVOKABLE QVariantList backupPreview' src/SystemBackend.h
grep -q 'Q_INVOKABLE QVariantList operationHistoryEntries' src/SystemBackend.h
grep -q 'Q_INVOKABLE QString flatpakIconPath' src/SystemBackend.h
if grep -q 'launchQuickAction\|sessionAction\|launchFlatpakManager' src/SystemBackend.h src/SystemBackend.cpp; then
  echo "ERROR: dead SystemBackend APIs remain" >&2
  exit 1
fi
if grep -q 'launchUnprivileged\|isUnprivilegedInvocationAllowed' src/PolkitHelper.h src/PolkitHelper.cpp; then
  echo "ERROR: dead Polkit unprivileged path remains" >&2
  exit 1
fi

# Next-boot selection is one-shot and the root helper validates the complete argv.
grep -q 'QStringLiteral("boot-next-uefi")' src/SystemBackend.cpp
grep -q 'QStringLiteral("boot-next-grub")' src/SystemBackend.cpp
grep -q 'execProgram("/usr/bin/efibootmgr"' src/AdminHelper.cpp
grep -q 'execProgram("/usr/bin/grub2-reboot"' src/AdminHelper.cpp
grep -q 'validBootToken' src/AdminHelper.cpp
grep -q 'validGrubEntry' src/AdminHelper.cpp
grep -q "value.startsWith(QLatin1Char('-'))" src/AdminHelper.cpp
grep -q 'SystemBackend.selectNextUefi' qml/modules/SystemModule.qml
grep -q 'SystemBackend.selectNextGrub' qml/modules/SystemModule.qml
grep -q 'Q_PROPERTY(QVariantList uefiEntries' src/SystemBackend.h
grep -q 'Q_PROPERTY(QVariantList grubEntries' src/SystemBackend.h
grep -q 'BootNext vale per un solo riavvio' qml/modules/SystemModule.qml
if grep -q 'efibootmgr.*-[Oo]' src/AdminHelper.cpp qml/modules/SystemModule.qml; then
  echo "ERROR: permanent UEFI BootOrder mutation must not be exposed" >&2
  exit 1
fi

# Minimal scope: no firmware updater or first-run/welcome workflow is shipped by the new system page.
if grep -niE 'fwupdmgr|firmware|welcome|first.?run' qml/modules/SystemModule.qml qml/modules/DashboardModule.qml qml/modules/RecoveryModule.qml; then
  echo "ERROR: firmware/welcome scope leaked into krisCC 0.7 UI" >&2
  exit 1
fi

# QML must never construct or launch privileged commands directly.
if grep -R -n 'PolkitHelper\|/usr/bin/bootc\|/usr/bin/dnf5\|/usr/bin/efibootmgr\|/usr/bin/grub2-reboot' qml; then
  echo "ERROR: privileged implementation details leaked into QML" >&2
  exit 1
fi
grep -q 'm_polkit->execute(QStringLiteral("/usr/bin/rk")' src/RkBackend.cpp
grep -q 'm_polkit->execute(QStringLiteral("/usr/libexec/kriscc/admin")' src/SoftwareBackend.cpp
grep -q 'm_polkit->execute(QStringLiteral("/usr/libexec/kriscc/admin")' src/BootcBackend.cpp
grep -q 'm_polkit->execute(QStringLiteral("/usr/libexec/kriscc/admin")' src/SystemBackend.cpp
grep -q 'QStringLiteral("status"), QStringLiteral("--json")' src/RkBackend.cpp
grep -q 'object.value(QStringLiteral("schema")).toInt(-1) != 1' src/RkBackend.cpp

# Dashboard resources are lightweight local reads; RAM explicitly uses MemAvailable and not swap.
grep -q 'Q_PROPERTY(int cpuUsagePercent' src/SystemBackend.h
grep -q 'Q_PROPERTY(qint64 memoryUsedMiB' src/SystemBackend.h
grep -q 'Q_PROPERTY(double cpuTemperatureC' src/SystemBackend.h
grep -q 'MemAvailable:' src/SystemBackend.cpp
grep -q '/sys/class/hwmon' src/SystemBackend.cpp
grep -q 'SystemBackend.cpuUsagePercent' qml/modules/DashboardModule.qml
grep -q 'SystemBackend.memoryUsedMiB' qml/modules/DashboardModule.qml
grep -q 'SystemBackend.cpuTemperatureC' qml/modules/DashboardModule.qml
grep -q 'swap esclusa' qml/modules/DashboardModule.qml
grep -q 'Q_INVOKABLE void setResourceMonitoringEnabled' src/SystemBackend.h
grep -q 'if (!m_resourceMonitoringEnabled)' src/SystemBackend.cpp
grep -q 'SystemBackend.setResourceMonitoringEnabled(root.visible && root.currentSection === 0)' qml/Main.qml
if grep -q 'acpitz\|sensorName.contains(QStringLiteral("soc"))' src/SystemBackend.cpp; then
  echo "ERROR: CPU temperature accepts a non-CPU fallback sensor" >&2
  exit 1
fi

# Personal commands are versioned user configuration and never cross the privilege boundary.
grep -q 'src/CustomActionsBackend.cpp src/CustomActionsBackend.h' CMakeLists.txt
grep -q 'QStandardPaths::AppConfigLocation' src/CustomActionsBackend.cpp
grep -q 'custom-actions.json' src/CustomActionsBackend.cpp
grep -q 'QSaveFile' src/CustomActionsBackend.cpp
grep -q 'geteuid() == 0' src/CustomActionsBackend.cpp
grep -q 'QStringLiteral("--noprofile")' src/CustomActionsBackend.cpp
grep -q 'QStringLiteral("--norc")' src/CustomActionsBackend.cpp
grep -q 'const QString kShell = QStringLiteral("/usr/bin/bash")' src/CustomActionsBackend.cpp
grep -q 'setChildProcessModifier' src/CustomActionsBackend.cpp
grep -q 'setsid()' src/CustomActionsBackend.cpp
grep -q 'kill(-pid' src/CustomActionsBackend.cpp
grep -q 'ids.contains(id)' src/CustomActionsBackend.cpp
grep -q 'ReadOwner | QFileDevice::WriteOwner' src/CustomActionsBackend.cpp
if grep -q 'PolkitHelper\|pkexec' src/CustomActionsBackend.cpp src/CustomActionsBackend.h; then
  echo "ERROR: personal commands must never use the privileged path" >&2
  exit 1
fi
grep -q 'Miei comandi' qml/modules/CommandsModule.qml
grep -q 'CustomActionsBackend.saveAction' qml/modules/CommandsModule.qml
grep -q 'CustomActionsBackend.runAction' qml/modules/CommandsModule.qml
for removed in top-cpu top-memory flatpak-list podman-images; do
  if grep -q "id: \"$removed\"" qml/modules/CommandsModule.qml; then
    echo "ERROR: duplicated bookmark remains: $removed" >&2
    exit 1
  fi
done

# Maintenance is deliberately unprivileged: even manual root execution is refused.
grep -q 'geteuid() == 0' src/MaintenanceHelper.cpp
grep -q 'esecuzione come root rifiutata' src/MaintenanceHelper.cpp
grep -q 'process->start(QStringLiteral("/usr/libexec/kriscc/maintenance")' src/MaintenanceBackend.cpp
if grep -q 'PolkitHelper\|pkexec\|PKEXEC_UID' src/MaintenanceBackend.cpp src/MaintenanceBackend.h src/MaintenanceHelper.cpp; then
  echo "ERROR: trash maintenance crossed back into the privileged path" >&2
  exit 1
fi

# The root helper is a one-purpose validator/executor; direct mutable tools never reach Polkit.
grep -q 'src/AdminHelper.cpp' CMakeLists.txt
grep -q 'geteuid() != 0' src/AdminHelper.cpp
grep -q 'execv(' src/AdminHelper.cpp
if grep -q '/usr/bin/bash\|/usr/bin/sh' src/AdminHelper.cpp; then
  echo "ERROR: admin helper must never execute a shell" >&2
  exit 1
fi
if grep -E 'org\.kriscc\.controlcenter\.(maintenance|dnf|boot\.next|bootc\.upgrade)' data/org.kriscc.controlcenter.policy; then
  echo "ERROR: obsolete direct-tool Polkit actions remain" >&2
  exit 1
fi

# Privileged processes are bounded and history does not retain full sensitive argv.
grep -q 'kLongTimeoutMs = 30 \* 60 \* 1000' src/PolkitHelper.cpp
grep -q 'kMaxOutput = 256 \* 1024' src/PolkitHelper.cpp
grep -q 'setChildProcessModifier' src/PolkitHelper.cpp
grep -q 'operationLabel()' src/PolkitHelper.cpp
grep -q 'kMaxLogBytes' src/OperationLog.cpp
grep -q 'ReadOwner | QFileDevice::WriteOwner' src/OperationLog.cpp

# D-Bus mutations must be allowed to trigger interactive Polkit authorization.
test "$(grep -c 'setInteractiveAuthorizationAllowed(true)' src/SystemBackend.cpp)" -ge 2

# --background must be a single activatable session instance, not an unreachable duplicate.
grep -q 'org.kriscc.ControlCenter' src/main.cpp
grep -q 'registerService(serviceName)' src/main.cpp
grep -q 'existing.call(QDBus::NoBlock, QStringLiteral("show"))' src/main.cpp
grep -q 'src/InstanceController.cpp src/InstanceController.h' CMakeLists.txt
grep -q 'Q_CLASSINFO("D-Bus Interface", "org.kriscc.ControlCenter")' src/InstanceController.h
grep -q 'qml/modules/SystemModule.qml' CMakeLists.txt
test ! -e qml/modules/BootcModule.qml
test ! -e qml/modules/ToolsModule.qml
grep -q 'pageStack.globalToolBar.style: Kirigami.ApplicationHeaderStyle.None' qml/Main.qml
grep -q 'StackLayout' qml/Main.qml
if grep -q 'pageStack.replace(' qml/Main.qml; then
  echo "ERROR: top-level navigation still recreates pages" >&2
  exit 1
fi
grep -q 'qmlRegisterType<UtilityBackend>("org.kriscc"' src/main.cpp
if grep -q 'setContextProperty(QStringLiteral("UtilityBackend")' src/main.cpp; then
  echo "ERROR: global UtilityBackend singleton restored" >&2
  exit 1
fi
for qml in SoftwareModule FlatpakModule PodmanModule SystemModule CommandsModule RecoveryModule; do
  grep -q 'UtilityBackend { id: utilityBackend }' "qml/modules/$qml.qml"
done
if grep -q 'QT_QML_SKIP_CACHEGEN' CMakeLists.txt; then
  echo "ERROR: SoftwareModule still bypasses qmlcachegen" >&2
  exit 1
fi
if grep -q '#c62828' qml/Main.qml; then
  echo "ERROR: hard-coded application accent returned" >&2
  exit 1
fi

# The released RPM is validated in a fresh Fedora job before release publication.
grep -q '^  rpm-smoke:' .github/workflows/build.yml
grep -Fq 'needs: [build-fedora, rpm-smoke, release-audit]' .github/workflows/build.yml
grep -Fq "dnf -y install \"\$rpm_file\"" .github/workflows/build.yml
grep -q '/usr/bin/krisCC' .github/workflows/build.yml

echo "Checking mandatory local DNF5 behavior..."
dnf5 list --installed --json >/dev/null

echo "Checking repository-backed DNF5 queries when metadata is available..."
if dnf5 repo list --all --json >/dev/null 2>&1; then
  dnf5 repo list --all --json >/dev/null
  if dnf5 repoquery --available --latest-limit=1 \
      --queryformat $'%{name}\t%{summary}\t%{evr}\t%{repoid}\t%{arch}\t%{downloadsize}\t%{installsize}\n' \
      'bash*' > /tmp/kriscc-repoquery.txt 2>/tmp/kriscc-repoquery.err; then
    grep -q '^bash' /tmp/kriscc-repoquery.txt || echo "WARNING: bash not returned by optional repoquery probe"
    if grep -q '^bash' /tmp/kriscc-repoquery.txt; then
      awk -F '\t' 'NR == 1 { exit (NF >= 7 ? 0 : 1) }' /tmp/kriscc-repoquery.txt \
        || { echo "ERROR: repoquery metadata fields are not tab-separated" >&2; exit 1; }
    fi
  else
    echo "WARNING: optional repoquery probe skipped (repository metadata/network unavailable)"
  fi
  dnf5 list --upgrades --json >/dev/null 2>&1 || echo "WARNING: optional upgrades probe unavailable"
  dnf5 list --recent --json >/dev/null 2>&1 || echo "WARNING: optional recent-packages probe unavailable"
  dnf5 config-manager --help >/dev/null 2>&1 || { echo "ERROR: dnf5 config-manager runtime is unavailable" >&2; exit 1; }
else
  echo "WARNING: repository metadata unavailable; optional DNF5 probes skipped"
fi

echo "Checking the BootC CLI contract used by the allowlist..."
bootc upgrade --help > /tmp/kriscc-bootc-upgrade-help.txt
for flag in --check --download-only --from-downloaded --apply; do
  grep -Fq -- "$flag" /tmp/kriscc-bootc-upgrade-help.txt \
    || { echo "ERROR: installed bootc does not support $flag" >&2; exit 1; }
done
bootc status --help > /tmp/kriscc-bootc-status-help.txt
grep -Fq -- '--format-version' /tmp/kriscc-bootc-status-help.txt \
  || { echo "ERROR: installed bootc does not support --format-version" >&2; exit 1; }
