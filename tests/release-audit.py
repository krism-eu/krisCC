#!/usr/bin/env python3
from __future__ import annotations

import re
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERSION = "0.7.0"
RELEASE = "7"
RPM_EVR = f"{VERSION}-{RELEASE}.fc44"
RPM_FILE = f"krisCC-{RPM_EVR}.x86_64.rpm"

def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")

def require(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)

cmake = read("CMakeLists.txt")
spec = read("packaging/krisCC.spec")
workflow = read(".github/workflows/build.yml")
main_cpp = read("src/main.cpp")
admin_cpp = read("src/AdminHelper.cpp")
admin_policy = read("src/AdminPolicy.cpp")
runner = read("src/PrivilegedProcessRunner.cpp")
polkit = read("src/PolkitHelper.cpp")
rk = read("src/RkBackend.cpp")
bootc = read("src/BootcBackend.cpp")
software_cpp = read("src/SoftwareBackend.cpp")
utility = read("src/UtilityBackend.cpp")
system_cpp = read("src/SystemBackend.cpp")
system_h = read("src/SystemBackend.h")
package_cpp = read("src/PackageSearch.cpp")
package_h = read("src/PackageSearch.h")
custom_cpp = read("src/CustomActionsBackend.cpp")
operation_log = read("src/OperationLog.cpp")
main_qml = read("qml/Main.qml")
dashboard = read("qml/modules/DashboardModule.qml")
software = read("qml/modules/SoftwareModule.qml")
flatpak = read("qml/modules/FlatpakModule.qml")
podman = read("qml/modules/PodmanModule.qml")
system = read("qml/modules/SystemModule.qml")
recovery = read("qml/modules/RecoveryModule.qml")
commands = read("qml/modules/CommandsModule.qml")

# Artifact identity.
m = re.search(r"project\(krisCC\s+VERSION\s+([0-9.]+)", cmake, re.S)
require(m and m.group(1) == VERSION, "CMake project version mismatch")
require(f"set(KRISCC_RELEASE {RELEASE})" in cmake, "CMake release mismatch")
require('KRISCC_VERSION="' + "$" + '{PROJECT_VERSION}-' + "$" + '{KRISCC_RELEASE}"' in cmake,
        "application version is not derived from the audited CMake values")
m = re.search(r"^Version:\s*(\S+)", spec, re.M)
require(m and m.group(1) == VERSION, "RPM Version mismatch")
m = re.search(r"^Release:\s*([0-9]+)%\{\?dist\}", spec, re.M)
require(m and m.group(1) == RELEASE, "RPM Release mismatch")
require(RPM_FILE in workflow and RPM_EVR in workflow, "CI RPM identity mismatch")
require("setApplicationVersion(QStringLiteral(KRISCC_VERSION))" in main_cpp,
        "UI version is not sourced from KRISCC_VERSION")

# Executable tests, not source-grep correctness tests.
for token in ("kriscc-test-validators", "tests/test_validators.cpp",
              "kriscc-test-admin-policy", "tests/test_admin_policy.cpp",
              "add_test(NAME validators", "add_test(NAME admin-policy"):
    require(token in cmake, f"missing executable test: {token}")
require("ctest --test-dir build --output-on-failure" in workflow,
        "CI does not execute Qt unit tests")

# Shared validators.
require("Validators::packageName" in rk, "RkBackend validator drift")
require("Validators::repositoryId" in software_cpp
        and "Validators::repositoryUrl" in software_cpp,
        "SoftwareBackend validator drift")
for token in ("Validators::packageName", "Validators::repositoryUrl",
              "Validators::bootToken", "Validators::grubEntry"):
    require(token in admin_policy, f"AdminPolicy missing shared validator: {token}")

# One privileged mutation boundary.
programs = set()
for backend in (rk, bootc, software_cpp, system_cpp):
    programs.update(re.findall(r'm_polkit->execute\(QStringLiteral\("([^"]+)"\)', backend))
require(programs == {"/usr/libexec/kriscc/admin"},
        f"unexpected privileged mutation paths: {sorted(programs)}")
require('program != QStringLiteral("/usr/libexec/kriscc/admin")' in polkit,
        "PolkitHelper does not restrict mutation path")
require('QStringLiteral("/usr/bin/rk")' not in polkit,
        "PolkitHelper still permits direct rk")
for token in ("bootc-check", "bootc-download", "bootc-prepare", "bootc-apply-downloaded",
              "rk-sync", "rk-add", "rk-rm", "rk-forget",
              "repo-enable", "repo-disable", "repo-add",
              "boot-next-uefi", "boot-next-grub"):
    require(f'QStringLiteral("{token}")' in admin_policy, f"missing admin operation: {token}")
for program in ("/usr/bin/bootc", "/usr/bin/rk", "/usr/bin/dnf5",
                "/usr/bin/efibootmgr", "/usr/bin/grub2-reboot"):
    require(program in admin_policy, f"missing fixed admin target: {program}")
require("/usr/bin/bash" not in admin_policy and "/usr/bin/sh" not in admin_policy,
        "AdminPolicy must not execute a shell")
require("geteuid() != 0" in admin_cpp and "buildAdminCommand" in admin_cpp,
        "admin root/policy gate missing")
for token in ("setStandardInputFile(QProcess::nullDevice())", "setsid()",
              "kill(-pid, SIGTERM)", "kill(-pid, SIGKILL)", "return 124"):
    require(token in runner, f"root timeout supervision missing: {token}")
require("kill(-pid" not in polkit and "terminateProcessGroup" not in polkit,
        "user Polkit process still owns an ineffective root timeout")
require("exitCode == 124" in polkit, "helper timeout is not reported")
require("code == 126 || code == 127" in runner,
        "child exit 126/127 still collides with pkexec semantics")
require("setStandardInputFile(QProcess::nullDevice())" in custom_cpp,
        "personal scripts still inherit stdin")

# Polkit surface.
root = ET.parse(ROOT / "data/org.kriscc.controlcenter.policy").getroot()
actions = {x.attrib["id"]: x for x in root.findall("action")}
require(set(actions) == {"org.kriscc.controlcenter.bootc.status",
                         "org.kriscc.controlcenter.admin"},
        f"unexpected Polkit actions: {sorted(actions)}")
require("auth_admin_keep" not in read("data/org.kriscc.controlcenter.policy"),
        "auth_admin_keep is forbidden")

# Structured state and lazy startup.
for token in ('QStringLiteral("status"), QStringLiteral("--json")',
              'object.value(QStringLiteral("schema")).toInt(-1) != 1',
              'QStringLiteral("pending_recovery")', 'QStringLiteral("needs_sync")'):
    require(token in rk, f"rk structured contract missing: {token}")
require("QTimer::singleShot(0, this, &RkBackend::refreshStatus)" not in rk,
        "RkBackend eager refresh returned")
require("QTimer::singleShot(0, this, &BootcBackend::refreshStatus)" not in bootc,
        "BootcBackend eager refresh returned")
require('QStringLiteral("--format-version=1")' in bootc, "BootC schema v1 not pinned")

# RPM memory/complete-list fixes.
require("entries.size() >= 500" not in package_cpp, "silent 500 RPM cap remains")
require("entries.size() >= 100" in package_cpp and "m_truncated = true" in package_cpp,
        "search cap/truncation state missing")
require("Q_PROPERTY(bool truncated" in package_h
        and "searchModel.truncated" in software
        and "primi 100 risultati" in software,
        "search truncation is not visible")
require('QStringLiteral("--eval"), QStringLiteral("%{_dbpath}")' in package_cpp,
        "rpm _dbpath is not authoritative")
require("/usr/share/rpm/rpmdb.sqlite" in package_cpp, "Fedora 44 rpmdb fallback missing")
require("preferredHeight: contentHeight" not in software, "RPM ListView not virtualized")
require("Novità repository" not in software, "repository-news tab remains")
require('qsTr("Overlay non richiesti")' in software, "misleading Locali label remains")

# Flatpak and Podman.
require("preferredHeight: contentHeight" not in flatpak, "Flatpak ListView not virtualized")
require("SystemBackend.launchFlatpak(modelData[1])" in flatpak
        and "Q_INVOKABLE bool launchFlatpak" in system_h,
        "Flatpak launch action missing")
require('QStringLiteral("run"), QStringLiteral("--user"), id' in system_cpp,
        "Flatpak launcher escaped user scope")
require('typeof item.Size === "object"' in podman and "rootFsSize" in podman and "rwSize" in podman,
        "Podman Size object not handled")
require('mode == QStringLiteral("remove")' in utility
        and '{QStringLiteral("rm"), name}' in utility,
        "Podman container rm missing")
require('"--force"' not in utility, "forced Podman removal is forbidden")

# Dashboard/UI fixes.
require("storageSummary READ storageSummary NOTIFY resourcesChanged" in system_h,
        "storageSummary remains constant")
require('QStringLiteral("firewalld.service")' in system_cpp and 'qsTr("Firewall")' in dashboard,
        "firewall dashboard health missing")
require("topMemoryProcesses" in system_h and "SystemBackend.topMemoryProcesses" in dashboard,
        "top RAM process view missing")
for icon in ('source: "cpu"', 'source: "memory"', 'source: "temperature"'):
    require(icon in dashboard, f"dashboard resource icon missing: {icon}")
require("property bool active" in dashboard and "onActiveChanged" in dashboard,
        "dashboard refresh-on-visible missing")
for name, qml in {"Dashboard": dashboard, "Software": software, "Flatpak": flatpak,
                  "Container": podman, "Sistema": system,
                  "Backup": recovery, "Comandi": commands}.items():
    require("PageIntro {" not in qml, f"{name}: duplicated title remains")
require("bootTechnicalDetails" not in system
        and "outputText: BootcBackend.statusText" not in system,
        "raw BootC JSON remains in normal UI")
require("aboutDialog.open()" in main_qml and "SystemBackend.quickSystemInfo()" in main_qml,
        "Information action is not real")
require("horizontalAlignment: Text.AlignRight" in main_qml,
        "header version is not right aligned")
require('qsTr("Needs sync")' not in recovery, "untranslated Needs sync remains")

# Backup/privacy/history.
for token in ('QStringLiteral(".var/app/*/cache")', "umask(0077)",
              "ReadOwner | QFileDevice::WriteOwner", "removeSnapshot"):
    require(token in system_cpp or token in system_h, f"backup hardening missing: {token}")
require("SystemBackend.removeSnapshot" in recovery, "backup delete UI missing")
require("font.bold: modelData.included" not in recovery, "backup content rows still bold")
require("ReadOwner | QFileDevice::WriteOwner" in operation_log, "history file not private")
require("operationDetail()" in polkit, "privileged history target missing")
require('OperationLog::append(QStringLiteral("krisCC"), completedOperation, state, m_title)' in utility,
        "user mutation history detail missing")

# QML never launches privileged tools directly.
for path in ("qml/modules/SystemModule.qml", "qml/modules/RecoveryModule.qml",
             "qml/modules/SoftwareModule.qml", "qml/modules/FlatpakModule.qml",
             "qml/modules/PodmanModule.qml", "qml/modules/CommandsModule.qml"):
    qml = read(path)
    require("PolkitHelper" not in qml, f"{path}: PolkitHelper leaked into QML")
    require(not re.search(r"/usr/(?:s?bin|libexec)/(?:bootc|dnf5|rk|efibootmgr|grub2-reboot)", qml),
            f"{path}: privileged executable leaked into QML")

print("release-audit: PASS")
