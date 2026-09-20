#!/usr/bin/env python3
from __future__ import annotations

import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERSION = "0.6.0"
RELEASE = "2"
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
system_cpp = read("src/SystemBackend.cpp")
polkit_cpp = read("src/PolkitHelper.cpp")
rk_cpp = read("src/RkBackend.cpp")
maintenance_backend_cpp = read("src/MaintenanceBackend.cpp")
maintenance_helper_cpp = read("src/MaintenanceHelper.cpp")
maintenance_trash_cpp = read("src/MaintenanceTrash.cpp")
utility_cpp = read("src/UtilityBackend.cpp")
main_cpp = read("src/main.cpp")
package_cpp = read("src/PackageSearch.cpp")
main_qml = read("qml/Main.qml")
dashboard_qml = read("qml/modules/DashboardModule.qml")
software_qml = read("qml/modules/SoftwareModule.qml")
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
require(TAG in workflow, "workflow does not publish the expected immutable tag")
require("0[.]6[.]0-1" not in workflow,
        "workflow still contains the previous release in an escaped regex")
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

# Privileged entry points are deliberately tiny and explicit.
expected_programs = {
    "/usr/bin/rk",
    "/usr/bin/bootc",
    "/usr/bin/dnf5",
    "/usr/bin/efibootmgr",
    "/usr/bin/grub2-reboot",
    "/usr/libexec/kriscc/maintenance",
}
privileged_programs = set()
for qml_path in ("qml/modules/SystemModule.qml", "qml/modules/RecoveryModule.qml",
                 "qml/modules/SoftwareModule.qml"):
    qml = read(qml_path)
    privileged_programs.update(re.findall(r'PolkitHelper\.execute\("([^"]+)"', qml))
    privileged_programs.update(re.findall(r'root\.runPrivileged\("([^"]+)"', qml))
    privileged_programs.update(re.findall(r'root\.requestPrivileged\(\s*"([^"]+)"', qml))
for backend in (rk_cpp, maintenance_backend_cpp):
    privileged_programs.update(re.findall(
        r'm_polkit->execute\(QStringLiteral\("([^"]+)"\)', backend))
require(privileged_programs == expected_programs,
        f"unexpected privileged entry points: {sorted(privileged_programs)}")
for program in expected_programs:
    require(f'program == QStringLiteral("{program}")' in polkit_cpp,
            f"PolkitHelper does not explicitly allowlist {program}")
require('args.size() == 2 && args.at(0) == QStringLiteral("-n")' in polkit_cpp,
        "UEFI BootNext invocation is not exact")
require('args.size() == 1 && isSafeGrubEntry(args.at(0))' in polkit_cpp,
        "GRUB next-entry invocation is not exact")
require('args.at(0) == QStringLiteral("forget")' in polkit_cpp,
        "rk forget is not explicitly allowlisted")
for maintenance_mode in ("trash-home", "trash-system", "trash-all"):
    require(f'QStringLiteral("{maintenance_mode}")' in polkit_cpp,
            f"maintenance mode not explicitly allowlisted: {maintenance_mode}")
require('program == QStringLiteral("/usr/libexec/kriscc/maintenance")' in polkit_cpp,
        "maintenance helper is not a dedicated privileged entry point")
require('args.at(0) == QStringLiteral("config-manager")' in polkit_cpp,
        "DNF repository mutations are not restricted to config-manager")
require("isSafeRepositoryId" in polkit_cpp and "isSafeRepositoryUrl" in polkit_cpp,
        "DNF repository validators are missing")
require('url.scheme() == QStringLiteral("https")' in polkit_cpp
        and 'url.userInfo().isEmpty()' in polkit_cpp
        and 'QStringLiteral("http")' not in polkit_cpp,
        "DNF repository URLs must be HTTPS-only and reject embedded credentials")
require("entry.startsWith(QLatin1Char('-'))" in polkit_cpp,
        "GRUB entry validator does not reject option-shaped values")
for forbidden in ('QStringLiteral("-o")', 'QStringLiteral("-O")', "--bootorder"):
    require(forbidden not in polkit_cpp, f"permanent UEFI ordering primitive exposed: {forbidden}")
require("/usr/bin/bash" not in polkit_cpp and "/usr/bin/sh" not in polkit_cpp,
        "privileged helper must never execute a shell")

# Policy shape is checked structurally, not by grep.
policy_root = ET.parse(ROOT / "data/org.kriscc.controlcenter.policy").getroot()
actions = {node.attrib["id"]: node for node in policy_root.findall("action")}
expected_actions = {
    "org.kriscc.controlcenter.bootc.status": ("/usr/libexec/kriscc/bootc-status", None, "yes"),
    "org.kriscc.controlcenter.rk.sync": ("/usr/bin/rk", "sync", "auth_admin"),
    "org.kriscc.controlcenter.rk.add": ("/usr/bin/rk", "add", "auth_admin"),
    "org.kriscc.controlcenter.rk.rm": ("/usr/bin/rk", "rm", "auth_admin"),
    "org.kriscc.controlcenter.rk.forget": ("/usr/bin/rk", "forget", "auth_admin"),
    "org.kriscc.controlcenter.maintenance.trash-home": ("/usr/libexec/kriscc/maintenance", "trash-home", "auth_admin"),
    "org.kriscc.controlcenter.maintenance.trash-system": ("/usr/libexec/kriscc/maintenance", "trash-system", "auth_admin"),
    "org.kriscc.controlcenter.maintenance.trash-all": ("/usr/libexec/kriscc/maintenance", "trash-all", "auth_admin"),
    "org.kriscc.controlcenter.dnf.config-manager": ("/usr/bin/dnf5", "config-manager", "auth_admin"),
    "org.kriscc.controlcenter.bootc.upgrade": ("/usr/bin/bootc", "upgrade", "auth_admin"),
    "org.kriscc.controlcenter.boot.next-uefi": ("/usr/bin/efibootmgr", "-n", "auth_admin"),
    "org.kriscc.controlcenter.boot.next-grub": ("/usr/bin/grub2-reboot", None, "auth_admin"),
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
require("src/MaintenanceHelper.cpp" in cmake
        and "install(TARGETS kriscc-maintenance" in cmake,
        "maintenance helper is not built and installed by CMake")
require("%{_libexecdir}/kriscc/maintenance" in spec,
        "maintenance helper is missing from RPM files")
for token in (
    "geteuid() != 0",
    'qEnvironmentVariable("PKEXEC_UID")',
    "arguments.size() != 2",
    'QStringLiteral("trash-home")',
    'QStringLiteral("trash-system")',
    'QStringLiteral("trash-all")',
    "QStorageInfo::mountedVolumes()",
    'storage.device().startsWith("/dev/")',
):
    require(token in maintenance_helper_cpp, f"maintenance safety invariant missing: {token}")
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

# RK recovery state is parsed once in C++ and QML consumes typed properties.
for token in ("Overlay:", "Pending recovery:", "Needs sync:"):
    require(f'QStringLiteral("{token}")' in rk_cpp,
            f"RkBackend parser is missing contract marker: {token}")
require('m_polkit->execute(QStringLiteral("/usr/bin/rk"), args)' in rk_cpp,
        "RkBackend does not own privileged rk recovery actions")
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
        and "bootProgressLines" in system_qml,
        "System BootC workflow lost staged/progress/refresh state")
require('QStringLiteral("downloadOnly")' in read("src/BootcBackend.cpp")
        and '["upgrade", "--from-downloaded", "--apply"]' in system_qml
        and '["upgrade", "--apply"]' not in system_qml
        and "SystemBackend.requestReboot()" in system_qml,
        "System BootC staged actions do not match the JSON deployment state")
require('QStringLiteral("--format-version=1")' in read("src/BootcBackend.cpp"),
        "root BootC JSON status path does not pin schema version 1")
require('QStringLiteral("--from-downloaded")' in polkit_cpp,
        "BootC allowlist is missing the fixed from-downloaded forms")
require('{QStringLiteral("upgrade"), QStringLiteral("--apply")}' not in polkit_cpp,
        "BootC allowlist still exposes the obsolete direct apply form")
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

# Keep the intended minimal scope and immutable KrisOS update contract.
combined_ui = system_qml + recovery_qml + dashboard_qml
require(not re.search(r"fwupdmgr|firmware|welcome|first.?run", combined_ui, re.I),
        "firmware/welcome scope leaked into 0.6.0")
require("bootc" in spec and "dnf5" in spec and "dnf5-plugins" in spec and "tar" in spec,
        "mandatory runtime requirements missing from RPM spec")
require("sudo rk sync" not in recovery_qml, "UI incorrectly claims sudo is used")
require("bootc" in readme.lower() and "rk" in integration_doc,
        "integration documentation lost KrisOS contracts")

print(f"krisCC release audit passed: {VERSION}-{RELEASE}")
