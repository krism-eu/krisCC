#!/usr/bin/env python3
from __future__ import annotations

import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERSION = "0.7.0"
RELEASE = "3"
RPM_EVR = f"{VERSION}-{RELEASE}.fc44"
RPM_FILE = f"krisCC-{RPM_EVR}.x86_64.rpm"
TAG = f"v{VERSION}-{RELEASE}"


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


cmake = read("CMakeLists.txt")
spec = read("packaging/krisCC.spec")
workflow = read(".github/workflows/build.yml")
promotion_workflow = read(".github/workflows/promote-stable.yml")
system_cpp = read("src/SystemBackend.cpp")
polkit_cpp = read("src/PolkitHelper.cpp")
admin_cpp = read("src/AdminHelper.cpp")
operation_log_cpp = read("src/OperationLog.cpp")
rk_cpp = read("src/RkBackend.cpp")
maintenance_backend_cpp = read("src/MaintenanceBackend.cpp")
maintenance_helper_cpp = read("src/MaintenanceHelper.cpp")
maintenance_trash_cpp = read("src/MaintenanceTrash.cpp")
utility_cpp = read("src/UtilityBackend.cpp")
main_cpp = read("src/main.cpp")
architecture = read("ARCHITECTURE.md")
package_cpp = read("src/PackageSearch.cpp")
main_qml = read("qml/Main.qml")
dashboard_qml = read("qml/modules/DashboardModule.qml")
software_qml = read("qml/modules/SoftwareModule.qml")
bootc_cpp = read("src/BootcBackend.cpp")
software_cpp = read("src/SoftwareBackend.cpp")
custom_cpp = read("src/CustomActionsBackend.cpp")
custom_h = read("src/CustomActionsBackend.h")
system_h = read("src/SystemBackend.h")
flatpak_qml = read("qml/modules/FlatpakModule.qml")
podman_qml = read("qml/modules/PodmanModule.qml")
system_qml = read("qml/modules/SystemModule.qml")
recovery_qml = read("qml/modules/RecoveryModule.qml")
commands_qml = read("qml/modules/CommandsModule.qml")
readme = read("README.md")
integration_doc = read("INTEGRAZIONE.md")
e2e = read("tests/e2e-readonly.sh")

# Version/package/release identity must agree everywhere that controls the artifact.
m = re.search(r"project\(krisCC\s+VERSION\s+([0-9.]+)", cmake, re.S)
require(m and m.group(1) == VERSION, "CMake project version mismatch")
m = re.search(r"^Version:\s*(\S+)", spec, re.M)
require(m and m.group(1) == VERSION, "RPM Version mismatch")
m = re.search(r"^Release:\s*([0-9]+)%\{\?dist\}", spec, re.M)
require(m and m.group(1) == RELEASE, "RPM Release mismatch")
require(RPM_FILE in workflow, "workflow does not pin the expected runtime RPM filename")
require(RPM_EVR in workflow, "workflow does not validate the expected RPM EVR")
require('tag="v${BASH_REMATCH[1]}-${BASH_REMATCH[2]}"' in workflow,
        "workflow does not derive the immutable tag from the validated RPM")
require("0.6.0" not in workflow and "0[.]6[.]0" not in workflow,
        "main workflow still contains 0.6 release literals")
require("branches: [main]" in workflow
        and "k1.0-ui-restyle" not in workflow
        and "kriscc-0.5-minimal" not in workflow,
        "main workflow still carries obsolete development branches")
require(f'<release version="{VERSION}"' in read("data/org.kriscc.KrisCC.metainfo.xml"),
        "AppStream metadata is missing the current version")
require(f"krisCC-{VERSION}-*.rpm" in readme, "README RPM version mismatch")
require(f"grep -Fxq 'Release:        {RELEASE}%{{?dist}}' packaging/krisCC.spec" in e2e,
        "e2e RPM release assertion mismatch")

# Only the consolidated pages are shipped. Legacy merged pages must be gone,
# not merely hidden from CMake.
require("qml/modules/SystemModule.qml" in cmake, "SystemModule is not shipped")
require(not (ROOT / "qml/modules/BootcModule.qml").exists(), "legacy BootcModule still exists")
require(not (ROOT / "qml/modules/ToolsModule.qml").exists(), "legacy ToolsModule still exists")
require("src/OperationLog.cpp src/OperationLog.h" in cmake, "OperationLog is not linked")
require("QT_QML_SKIP_CACHEGEN" not in cmake, "QML cache generation must not be bypassed")

# Every bookmark exposed by QML must have a backend implementation.
backend_bookmarks = set(re.findall(r'id == QStringLiteral\("([^"]+)"\)', utility_cpp))
qml_bookmarks = set()
for qml in (system_qml, recovery_qml, commands_qml):
    qml_bookmarks.update(re.findall(r'utilityBackend\.runBookmark\("([^"]+)"\)', qml))
command_card_ids = set(re.findall(r'\{\s*id:\s*"([^"]+)"', commands_qml))
missing = sorted((qml_bookmarks | command_card_ids) - backend_bookmarks)
require(not missing, f"QML bookmark(s) without backend implementation: {missing}")

# Privileged entry points are deliberately tiny, explicit and owned by typed backends.
expected_programs = {
    "/usr/bin/rk",
    "/usr/libexec/kriscc/admin",
}
for qml_path in ("qml/modules/SystemModule.qml", "qml/modules/RecoveryModule.qml",
                 "qml/modules/SoftwareModule.qml"):
    qml = read(qml_path)
    require("PolkitHelper" not in qml,
            f"{qml_path}: QML must not access PolkitHelper")
    require(not re.search(r'/(?:usr/)?bin/(?:bootc|dnf5|efibootmgr|grub2-reboot)', qml),
            f"{qml_path}: privileged implementation path leaked into QML")

require('setContextProperty(QStringLiteral("PolkitHelper")' not in main_cpp,
        "PolkitHelper must not be exposed to QML")

privileged_programs = set()
for backend in (rk_cpp, bootc_cpp, software_cpp, system_cpp):
    privileged_programs.update(re.findall(
        r'm_polkit->execute\(QStringLiteral\("([^"]+)"\)', backend))
require(privileged_programs == expected_programs,
        f"unexpected privileged entry points: {sorted(privileged_programs)}")
for program in expected_programs:
    require(f'program == QStringLiteral("{program}")' in polkit_cpp,
            f"PolkitHelper does not explicitly allowlist {program}")

for token in (
    'QStringLiteral("bootc-check")',
    'QStringLiteral("bootc-download")',
    'QStringLiteral("bootc-prepare")',
    'QStringLiteral("bootc-apply-downloaded")',
    'QStringLiteral("repo-enable")',
    'QStringLiteral("repo-disable")',
    'QStringLiteral("repo-add")',
    'QStringLiteral("boot-next-uefi")',
    'QStringLiteral("boot-next-grub")',
):
    require(token in polkit_cpp, f"semantic admin operation missing from PolkitHelper: {token}")
require("isSafeRepositoryId" in polkit_cpp and "isSafeRepositoryUrl" in polkit_cpp,
        "repository validators are missing from the client-side allowlist")
require('url.scheme() == QStringLiteral("https")' in polkit_cpp
        and 'url.userInfo().isEmpty()' in polkit_cpp,
        "repository URLs must be HTTPS-only and reject embedded credentials")

require("geteuid() != 0" in admin_cpp and "execv(" in admin_cpp,
        "root admin helper does not enforce privileged execution via exact exec")
for token in (
    'execProgram("/usr/bin/bootc"',
    'execProgram("/usr/bin/dnf5"',
    'execProgram("/usr/bin/efibootmgr"',
    'execProgram("/usr/bin/grub2-reboot"',
    "validRepositoryId",
    "validRepositoryUrl",
    "validBootToken",
    "validGrubEntry",
):
    require(token in admin_cpp, f"admin helper invariant missing: {token}")
require("/usr/bin/bash" not in admin_cpp and "/usr/bin/sh" not in admin_cpp,
        "admin helper must never execute a shell")
require("value.startsWith(QLatin1Char('-'))" in admin_cpp,
        "GRUB entry validator does not reject option-shaped values")
for forbidden in ('QStringLiteral("-o")', 'QStringLiteral("-O")', "--bootorder"):
    require(forbidden not in admin_cpp, f"permanent UEFI ordering primitive exposed: {forbidden}")

require("kLongTimeoutMs = 30 * 60 * 1000" in polkit_cpp
        and "kMaxOutput = 256 * 1024" in polkit_cpp
        and "setChildProcessModifier" in polkit_cpp
        and "operationLabel()" in polkit_cpp,
        "privileged process bounding/redaction is incomplete")

# Policy shape is checked structurally, not by grep.
policy_root = ET.parse(ROOT / "data/org.kriscc.controlcenter.policy").getroot()
actions = {node.attrib["id"]: node for node in policy_root.findall("action")}
expected_actions = {
    "org.kriscc.controlcenter.bootc.status": ("/usr/libexec/kriscc/bootc-status", None, "yes"),
    "org.kriscc.controlcenter.rk.sync": ("/usr/bin/rk", "sync", "auth_admin"),
    "org.kriscc.controlcenter.rk.add": ("/usr/bin/rk", "add", "auth_admin"),
    "org.kriscc.controlcenter.rk.rm": ("/usr/bin/rk", "rm", "auth_admin"),
    "org.kriscc.controlcenter.rk.forget": ("/usr/bin/rk", "forget", "auth_admin"),
    "org.kriscc.controlcenter.admin": ("/usr/libexec/kriscc/admin", None, "auth_admin"),
}
require(set(actions) == set(expected_actions), "Polkit action set changed unexpectedly")
for action_id, (path, argv1, allow_active) in expected_actions.items():
    action = actions[action_id]
    annotations = {a.attrib.get("key"): (a.text or "") for a in action.findall("annotate")}
    require(annotations.get("org.freedesktop.policykit.exec.path") == path,
            f"{action_id}: executable path mismatch")
    require(annotations.get("org.freedesktop.policykit.exec.argv1") == argv1,
            f"{action_id}: argv1 policy mismatch")
    defaults = action.find("defaults")
    require(defaults is not None, f"{action_id}: defaults missing")
    require(defaults.findtext("allow_any") == "no", f"{action_id}: allow_any must be no")
    require(defaults.findtext("allow_inactive") == "no", f"{action_id}: allow_inactive must be no")
    require(defaults.findtext("allow_active") == allow_active,
            f"{action_id}: allow_active mismatch")
require("auth_admin_keep" not in read("data/org.kriscc.controlcenter.policy"),
        "Polkit authorization retention is forbidden")

bootc_wrapper = read("src/bootc-status.sh")
require('case "$1" in' in bootc_wrapper
        and 'exec /usr/bin/bootc status --format json --format-version=1' in bootc_wrapper
        and 'exec /usr/bin/bootc status --format humanreadable' in bootc_wrapper
        and '"$@"' not in bootc_wrapper,
        "bootc status wrapper must expose only fixed status formats")
require("bootc-status.sh" in cmake,
        "bootc status wrapper is not installed by CMake")
require("%{_libexecdir}/kriscc/bootc-status" in spec,
        "bootc status wrapper is missing from RPM files")
require("src/AdminHelper.cpp" in cmake
        and "install(TARGETS kriscc-admin kriscc-maintenance" in cmake,
        "admin helper is not built and installed by CMake")
require("%{_libexecdir}/kriscc/admin" in spec,
        "admin helper is missing from RPM files")
require("src/MaintenanceHelper.cpp" in cmake
        and "install(TARGETS kriscc-admin kriscc-maintenance" in cmake,
        "maintenance helper is not built and installed by CMake")
require("%{_libexecdir}/kriscc/maintenance" in spec,
        "maintenance helper is missing from RPM files")
for token in (
    "geteuid() == 0",
    "esecuzione come root rifiutata",
    "arguments.size() != 2",
    'QStringLiteral("trash-home")',
    'QStringLiteral("trash-system")',
    'QStringLiteral("trash-all")',
    "QStorageInfo::mountedVolumes()",
    'storage.device().startsWith("/dev/")',
):
    require(token in maintenance_helper_cpp, f"maintenance safety invariant missing: {token}")
require("PolkitHelper" not in maintenance_backend_cpp
        and "pkexec" not in maintenance_backend_cpp
        and "PKEXEC_UID" not in maintenance_helper_cpp,
        "trash cleanup must remain unprivileged")
require('process->start(QStringLiteral("/usr/libexec/kriscc/maintenance")' in maintenance_backend_cpp,
        "maintenance backend must execute the helper directly as the user")
require("isSymLink()" in maintenance_trash_cpp
        and "Mount annidato ignorato per sicurezza" in maintenance_trash_cpp,
        "trash cleanup must reject symlink/mount traversal")

# Backup contract: canonical path validation, safe extraction and all async start failures
# must leave the UI out of the busy state.
for token in (
    "validateBackupPath",
    'canonical.startsWith(backupRoot + QLatin1Char(\'/\'))',
    'QStringLiteral("--no-same-owner")',
    'QStringLiteral("--no-same-permissions")',
    "QProcess::nullDevice()",
    'tr("Impossibile avviare la verifica: %1")',
    'tr("Impossibile avviare il ripristino: %1")',
):
    require(token in system_cpp, f"backup safety invariant missing: {token}")
require(system_cpp.count("&QProcess::errorOccurred") >= 3,
        "create/verify/restore must all handle FailedToStart")
require("setBackupBusy(false);" in system_cpp, "backup failure paths do not clear busy state")
require('QStringLiteral(".local/share/flatpak")' in system_cpp,
        "home backup must exclude Flatpak runtime/application store")
require('QStringLiteral(".local/share/containers")' in system_cpp,
        "home backup must exclude Podman container store")

# RK recovery state is consumed through the KrisOS versioned JSON contract.
for token in (
    'QStringLiteral("status"), QStringLiteral("--json")',
    'object.value(QStringLiteral("schema")).toInt(-1) != 1',
    'QStringLiteral("pending_recovery")',
    'QStringLiteral("needs_sync")',
    'QStringLiteral("requests")',
):
    require(token in rk_cpp, f"RkBackend JSON contract missing: {token}")
require("Overlay:" not in rk_cpp and "Pending recovery:" not in rk_cpp,
        "RkBackend still parses human-readable rk status")
require('m_polkit->execute(QStringLiteral("/usr/bin/rk"), args)' in rk_cpp,
        "RkBackend does not own privileged rk actions")
require("Q_INVOKABLE bool addPackage" in read("src/RkBackend.h")
        and "Q_INVOKABLE bool removePackage" in read("src/RkBackend.h"),
        "RkBackend does not own package mutations")
require('utilityBackend.runBookmark("rk-status")' not in recovery_qml,
        "Recovery still parses rk through the generic command backend")
require("RkBackend.needsSync" in recovery_qml and "RkBackend.overlayState" in dashboard_qml,
        "structured RK state is not wired into Recovery/Dashboard")

# Flatpak stays entirely in user scope and installs from the remote returned
# by search instead of forcing Flathub for every result.
require('mode == QStringLiteral("remove-unused")' in utility_cpp
        and 'QStringLiteral("--user")' in utility_cpp
        and 'QStringLiteral("--unused")' in utility_cpp,
        "Flatpak unused cleanup must stay in user scope")
require('QStringLiteral("search"), QStringLiteral("--user")' in utility_cpp,
        "Flatpak search must stay in user scope")
require('QStringLiteral("install"), QStringLiteral("--user"), QStringLiteral("--noninteractive")' in utility_cpp
        and 'QStringLiteral("--assumeyes")' in utility_cpp,
        "Flatpak install must be noninteractive in user scope")
require("selectedRemote" in utility_cpp and "modelData[5]" in flatpak_qml,
        "Flatpak install must preserve the search result remote")
require('currentIndex: root.mode' not in flatpak_qml
        and 'currentIndex: root.mode' not in podman_qml,
        "tab state must not bind currentIndex back to mode")

# Navigation must keep all seven pages alive and disable the Kirigami global
# page header to avoid duplicate chrome/title bars.
require("pageStack.globalToolBar.style: Kirigami.ApplicationHeaderStyle.None" in main_qml,
        "global Kirigami page toolbar is not disabled")
require("StackLayout" in main_qml and "pageStack.replace(" not in main_qml,
        "top-level pages must stay alive in a StackLayout")
require("qmlRegisterType<UtilityBackend>" in main_cpp,
        "UtilityBackend must be instantiable per page")
require('setContextProperty(QStringLiteral("UtilityBackend")' not in main_cpp,
        "global UtilityBackend context singleton must not be restored")
for qml in (software_qml, flatpak_qml, podman_qml, system_qml, commands_qml, recovery_qml):
    require("UtilityBackend { id: utilityBackend }" in qml,
            "each active page must own an isolated UtilityBackend")
require("#c62828" not in main_qml, "hard-coded red accent must not override the desktop theme")
for rare_id in ("rk-status", "uefi", "grub-entries", "fstab-order"):
    require(f'id: "{rare_id}"' not in commands_qml,
            f"rare system command leaked back into daily Commands page: {rare_id}")
for daily_id in ("journal-size", "inodes", "user-failed-units"):
    require(f'id: "{daily_id}"' in commands_qml,
            f"daily diagnostic command missing: {daily_id}")
require("utilityBackend.clearResult()" in commands_qml
        and "Q_INVOKABLE void clearResult()" in read("src/UtilityBackend.h"),
        "command output clear action is missing")

# Dialogs that live inside ScrollablePage must be reparented to the window overlay.
for name, qml in {
    "software": software_qml,
    "flatpak": flatpak_qml,
    "podman": podman_qml,
    "system": system_qml,
    "recovery": recovery_qml,
    "commands": commands_qml,
}.items():
    require(qml.count("parent: Controls.Overlay.overlay") >= qml.count("Controls.Dialog {"),
            f"{name}: dialog remains parented to scroll content")

# RPM plan is rendered as the real rk output; no locale-sensitive English parser.
for token in ("Installing dependencies:", "Installing weak dependencies:",
              "Transaction Summary:", "Total size of inbound packages"):
    require(token not in software_qml, f"locale-sensitive rk parser remains: {token}")
require("seen.contains(key)" in package_cpp and "name + QLatin1Char('\\x1f') + arch" in package_cpp,
        "RPM search must deduplicate by name+arch")
require('QStringLiteral("--latest-limit=1")' in package_cpp,
        "RPM search must request the latest candidate for each name.arch")
require("m_installedFilter" in package_cpp and "visibleForFilter" not in software_qml,
        "installed RPM filtering must happen in the model")
require("/usr/libexec/kriscc/bootc-status humanreadable" in utility_cpp,
        "health check must use the safe bootc status wrapper")
require("root.hasStagedDeployment()" in system_qml
        and "BootcBackend.refreshStatus()" in system_qml
        and "BootcBackend.operationLines" in system_qml,
        "System BootC workflow lost staged/progress/refresh state")
require('QStringLiteral("downloadOnly")' in bootc_cpp
        and "BootcBackend.applyDownloaded()" in system_qml
        and "BootcBackend.checkUpgrade()" in system_qml
        and "BootcBackend.downloadUpgrade()" in system_qml
        and "BootcBackend.prepareUpgrade()" in system_qml
        and "SystemBackend.requestReboot()" in system_qml,
        "System BootC staged actions do not match the typed backend state")
require('QStringLiteral("--format-version=1")' in bootc_cpp,
        "root BootC JSON status path does not pin schema version 1")
require('QStringLiteral("--from-downloaded")' in admin_cpp
        and 'QStringLiteral("bootc-apply-downloaded")' in admin_cpp,
        "admin helper is missing the fixed from-downloaded BootC form")
require('{QStringLiteral("upgrade"), QStringLiteral("--apply")}' not in admin_cpp,
        "admin helper exposes the obsolete direct apply form")
require("constexpr int kInteractiveTimeoutMs = 30 * 60 * 1000;" in utility_cpp
        and utility_cpp.count("kInteractiveTimeoutMs") >= 7,
        "Flatpak mutations are not consistently bounded by the interactive timeout")
require("constexpr int kPodmanActionTimeoutMs = 5 * 60 * 1000;" in utility_cpp
        and utility_cpp.count("kPodmanActionTimeoutMs") >= 5,
        "Podman actions are not consistently bounded by the action timeout")
require("launchQuickAction" not in system_cpp and "sessionAction" not in system_cpp,
        "dead SystemBackend APIs remain")
require("Q_PROPERTY(QString selinuxState" in read("src/SystemBackend.h")
        and "SystemBackend.selinuxState" in dashboard_qml,
        "Dashboard SELinux state is not backed by SystemBackend")
require("launchUnprivileged" not in polkit_cpp,
        "dead Polkit unprivileged launcher remains")

# Specialized boot state is parsed in SystemBackend, never in QML.
require("Q_PROPERTY(QVariantList uefiEntries" in system_h
        and "Q_PROPERTY(QVariantList grubEntries" in system_h,
        "typed next-boot entry state is missing")
require("SystemBackend.refreshUefiEntries()" in system_qml
        and "SystemBackend.refreshGrubEntries()" in system_qml
        and "SystemBackend.selectNextUefi" in system_qml
        and "SystemBackend.selectNextGrub" in system_qml,
        "System page does not use typed next-boot APIs")
require("function uefiEntries()" not in system_qml
        and "function grubEntries()" not in system_qml,
        "system-text parsing remains in QML")

# Dashboard keeps lightweight local resource state only while the visible overview needs it.
for token in (
    "Q_PROPERTY(int cpuUsagePercent",
    "Q_PROPERTY(qint64 memoryUsedMiB",
    "Q_PROPERTY(qint64 memoryTotalMiB",
    "Q_PROPERTY(double cpuTemperatureC",
):
    require(token in system_h, f"resource property missing: {token}")
require("MemAvailable:" in system_cpp,
        "RAM usage must use MemAvailable rather than swap or free-only accounting")
require('/sys/class/hwmon' in system_cpp and "k10temp" in system_cpp and "coretemp" in system_cpp,
        "CPU temperature must use local hwmon capability detection")
require("acpitz" not in system_cpp
        and 'sensorName.contains(QStringLiteral("soc"))' not in system_cpp,
        "CPU temperature accepts a non-CPU fallback sensor")
require("Q_INVOKABLE void setResourceMonitoringEnabled" in system_h
        and "if (!m_resourceMonitoringEnabled)" in system_cpp
        and "SystemBackend.setResourceMonitoringEnabled(root.visible && root.currentSection === 0)" in main_qml,
        "resource polling must stop while krisCC/dashboard is not visible")
for token in ("SystemBackend.cpuUsagePercent", "SystemBackend.memoryUsedMiB",
              "SystemBackend.cpuTemperatureC", "swap esclusa"):
    require(token in dashboard_qml, f"dashboard resource box missing: {token}")

# Personal commands are persistent user data and are intentionally outside the privileged contract.
require("src/CustomActionsBackend.cpp src/CustomActionsBackend.h" in cmake,
        "CustomActionsBackend is not linked")
require("QStandardPaths::AppConfigLocation" in custom_cpp
        and "custom-actions.json" in custom_cpp
        and "QSaveFile" in custom_cpp,
        "personal command storage is not versioned/atomic user configuration")
require("geteuid() == 0" in custom_cpp,
        "personal commands must refuse execution when krisCC itself is root")
require('QStringLiteral("--noprofile")' in custom_cpp
        and 'QStringLiteral("--norc")' in custom_cpp
        and 'const QString kShell = QStringLiteral("/usr/bin/bash")' in custom_cpp,
        "personal Bash scripts do not use the fixed bounded execution wrapper")
require("setChildProcessModifier" in custom_cpp and "setsid()" in custom_cpp
        and "kill(-pid" in custom_cpp,
        "personal script cancellation does not terminate the complete process group")
require("ids.contains(id)" in custom_cpp
        and "ReadOwner | QFileDevice::WriteOwner" in custom_cpp,
        "personal command file is not fail-closed/private")
require("PolkitHelper" not in custom_cpp and "pkexec" not in custom_cpp,
        "personal commands crossed the privileged boundary")
require("Miei comandi" in commands_qml
        and "CustomActionsBackend.saveAction" in commands_qml
        and "CustomActionsBackend.runAction" in commands_qml,
        "personal commands UI is missing")
for removed in ("top-cpu", "top-memory", "flatpak-list", "podman-images"):
    require(f'id: "{removed}"' not in commands_qml,
            f"duplicated predefined command remains: {removed}")

# Local history is bounded/private and D-Bus mutations can request interactive authorization.
require("kMaxLogBytes" in operation_log_cpp
        and "ReadOwner | QFileDevice::WriteOwner" in operation_log_cpp,
        "operation history is not bounded/private")
require(system_cpp.count("setInteractiveAuthorizationAllowed(true)") >= 2,
        "systemd/logind mutations cannot request interactive authorization")

# Current main supports only KrisOS runtime state paths; migration fallbacks are gone.
require("/usr/share/krisos/owned-packages.txt" in package_cpp
        and "/var/lib/krisos/packages.list" in package_cpp
        and "/var/lib/krisos/packages.list" in bootc_cpp,
        "current KrisOS state paths missing")
require("raku-kris" not in package_cpp and "raku-kris" not in bootc_cpp,
        "obsolete Raku compatibility paths remain")

# Keep the intended minimal scope and immutable KrisOS update contract.
combined_ui = system_qml + recovery_qml + dashboard_qml
require(not re.search(r"fwupdmgr|firmware|welcome|first.?run", combined_ui, re.I),
        "firmware/welcome scope leaked into 0.7.0")
require("bootc" in spec and "dnf5" in spec and "dnf5-plugins" in spec and "tar" in spec,
        "mandatory runtime requirements missing from RPM spec")
require("sudo rk sync" not in recovery_qml, "UI incorrectly claims sudo is used")
require("bootc" in readme.lower() and "rk" in integration_doc,
        "integration documentation lost KrisOS contracts")

require("workflow_dispatch:" in promotion_workflow,
        "stable promotion must be an explicit manual action")
require("host_acceptance_confirmed" in promotion_workflow,
        "stable promotion lacks the KrisOS host acceptance gate")
require('gh release edit "$TAG"' in promotion_workflow
        and "--prerelease=false" in promotion_workflow,
        "stable promotion must change the existing candidate instead of rebuilding it")
require("sha256sum -c SHA256SUMS" in promotion_workflow
        and "rpm -qp --qf" in promotion_workflow,
        "stable promotion does not verify the released RPM")
require("stable/0.6" in readme,
        "README does not preserve the frozen previous line")

for token in (
    "**Integrazione forte, dipendenze deboli.**",
    "QML non implementa logica di sistema.",
    "Le letture devono fallire in modo morbido",
    "Le mutazioni devono fallire in modo chiuso",
    "Definition of Done",
):
    require(token in architecture, f"architectural contract is missing: {token}")

print(f"krisCC release audit passed: {VERSION}-{RELEASE}")
