#!/usr/bin/env python3
from __future__ import annotations

import re
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")

def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)

version_cfg = read("cmake/KrisCCVersion.cmake")
m_v = re.search(r'KRISCC_VERSION\s+"([^"]+)"', version_cfg)
require(m_v, "missing canonical krisCC version")
VERSION = m_v.group(1)
require(re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", VERSION) is not None,
        "public krisCC version must be X.Y.Z only")
require("KRISCC_RELEASE" not in version_cfg,
        "RPM release must not be part of the public version source")

cmake = read("CMakeLists.txt")
spec = read("packaging/krisCC.spec")
workflow = read(".github/workflows/build.yml")
polkit_cpp = read("src/PolkitHelper.cpp")
admin_cpp = read("src/AdminHelper.cpp")
admin_policy = read("src/AdminPolicy.cpp")
rk_cpp = read("src/RkBackend.cpp")
custom_cpp = read("src/CustomActionsBackend.cpp")
utility_cpp = read("src/UtilityBackend.cpp")
package_cpp = read("src/PackageSearch.cpp")
system_cpp = read("src/SystemBackend.cpp")
software_qml = read("qml/modules/SoftwareModule.qml")
flatpak_qml = read("qml/modules/FlatpakModule.qml")
system_qml = read("qml/modules/SystemModule.qml")
dashboard_qml = read("qml/modules/DashboardModule.qml")
commands_qml = read("qml/modules/CommandsModule.qml")
recovery_qml = read("qml/modules/RecoveryModule.qml")
contract_parsers_h = read("src/ContractParsers.h")
contract_parsers_cpp = read("src/ContractParsers.cpp")
contract_parsers_test = read("tests/test_contract_parsers.cpp")

require(f'Version:        {VERSION}' in spec, "RPM Version differs from canonical version")
require('Release:        1%{?dist}' in spec,
        "RPM Release must stay fixed at 1; bump X.Y.Z instead")
require('KRISCC_VERSION="${PROJECT_VERSION}"' in cmake,
        "UI version must be exactly canonical X.Y.Z")
require('KrisCCVersion.cmake' in workflow
        and 'release="1"' in workflow
        and 'vr="${version}-${release}"' in workflow
        and 'echo "rpm=krisCC-${vr}.fc44.x86_64.rpm"' in workflow,
        "CI RPM identity is not derived from canonical X.Y.Z with fixed Release 1")
require('echo "source_zip=krisCC-${vr}-source.zip"' in workflow
        and 'echo "source_txt=krisCC-${vr}-source.txt"' in workflow,
        "CI source bundle identity is not derived from canonical version+release")
require('VERSION: ${{ steps.identity.outputs.version }}' in workflow
        and '"krisCC ${VERSION}"' in workflow,
        "CI does not verify the public X.Y.Z application version")
checkout_dependency = workflow.find('- name: Install checkout dependency')
first_checkout = workflow.find('- uses: actions/checkout@')
require(checkout_dependency >= 0 and first_checkout >= 0 and checkout_dependency < first_checkout,
        "build job must install Git before checkout so exact source snapshots have repository metadata")
require('fetch-depth: 1' in workflow,
        "build checkout must preserve the exact Git commit for source bundling")
require('python3 tools/source_snapshot.py' in workflow
        and 'git archive' in workflow
        and '--format=zip' in workflow,
        "CI does not generate both exact source TXT and ZIP from the build commit")
require('sha256sum "$RPM" "$SOURCE_ZIP" "$SOURCE_TXT" > SHA256SUMS' in workflow
        and 'test "$(wc -l < SHA256SUMS)" -eq 3' in workflow,
        "RPM/source ZIP/source TXT are not covered by one three-entry SHA256SUMS")
require('grep -Fxq "# commit: $GITHUB_SHA"' in workflow,
        "source transcript is not tied to the exact build commit")
require('tag="v${BASH_REMATCH[1]}"' in workflow
        and 'test "${BASH_REMATCH[2]}" = "1"' in workflow,
        "release tag must remain public X.Y.Z while RPM Release stays fixed at 1")
source_snapshot = read("tools/source_snapshot.py")
require('run_git(root, "ls-tree", "-r", "-z", "--full-tree", commit)' in source_snapshot
        and 'run_git(root, "cat-file", "blob", object_sha)' in source_snapshot
        and '# commit:' in source_snapshot
        and '# tree:' in source_snapshot
        and 'hashlib.sha256' in source_snapshot,
        "source TXT generator does not snapshot and fingerprint the exact tracked Git tree")
require(f'<release version="{VERSION}"' in read("data/org.kriscc.KrisCC.metainfo.xml"),
        "AppStream release is stale")

require("src/Validators.cpp src/Validators.h" in cmake, "shared validators not linked")
require("src/AdminPolicy.cpp src/AdminPolicy.h" in cmake, "shared admin policy not linked")
require("kriscc-test-validators" in cmake
        and "kriscc-test-admin-policy" in cmake
        and "kriscc-test-process-runner" in cmake
        and "kriscc-test-parsers" in cmake,
        "semantic unit tests are not wired into CTest")
require("src/ProcessRunner.cpp src/ProcessRunner.h" in cmake,
        "shared user-level ProcessRunner not linked")
require("src/ContractParsers.cpp src/ContractParsers.h" in cmake,
        "shared contract parsers not linked")

require("AdminPolicy::resolve" in admin_cpp and "AdminPolicy::resolve" in polkit_cpp,
        "client/root privileged allowlist does not share AdminPolicy")
require('QStringLiteral("/usr/bin/rk")' in admin_policy
        and 'QStringLiteral("/usr/bin/bootc")' in admin_policy
        and 'QStringLiteral("/usr/bin/dnf5")' in admin_policy,
        "AdminPolicy lost fixed executable mapping")
require("/usr/bin/bash" not in admin_policy and "/usr/bin/sh" not in admin_policy,
        "AdminPolicy must never expose a root shell")
require('QStringLiteral("cc-update")' not in admin_policy,
        "krisCC is image-owned and must not have a standalone privileged RPM updater")
require("setStandardInputFile(QProcess::nullDevice())" in admin_cpp,
        "root helper stdin must be closed")
require("SIGTERM" in admin_cpp and "SIGKILL" in admin_cpp and "return 124" in admin_cpp,
        "root helper timeout is not real/bounded")
require('m_polkit->execute(QStringLiteral("/usr/libexec/kriscc/admin")' in rk_cpp,
        "krisCC rk mutations must pass through the supervised admin helper")
require('m_polkit->execute(QStringLiteral("/usr/bin/rk")' not in rk_cpp,
        "krisCC must not launch privileged rk directly")
process_runner = read("src/ProcessRunner.cpp")
require("setStandardInputFile(QProcess::nullDevice())" in process_runner,
        "shared user-level ProcessRunner must close stdin")
require("ProcessRunner" in custom_cpp and "ProcessRunner" in utility_cpp,
        "custom actions and utility commands must use the shared ProcessRunner")
require("ProcessRunner" in system_cpp
        and "m_backupRunner" in read("src/SystemBackend.h")
        and "kBackupVerifyTimeoutMs" in system_cpp
        and "kBackupOperationTimeoutMs" in system_cpp,
        "backup operations must use the bounded shared ProcessRunner")
require("(exitCode == 0 || exitCode == 1)" not in system_cpp,
        "tar exit code 1 must never be published as a valid backup")
require('preflightOptions.arguments = {QStringLiteral("-tzf"), canonical};' in system_cpp,
        "restore must gate extraction behind an archive preflight")

require("options.mergedChannels = !structuredOutput;" in utility_cpp,
        "machine-readable utility output must be isolated from stderr")
for operation in ("flatpak.installed", "flatpak.system-installed", "flatpak.updates",
                  "flatpak.remotes", "flatpak.search", "podman.list", "podman.images"):
    require(operation in utility_cpp and "structuredOutput" in utility_cpp,
            f"{operation}: structured output protection missing")

policy = ET.parse(ROOT / "data/org.kriscc.controlcenter.policy").getroot()
actions = {node.attrib["id"]: node for node in policy.findall("action")}
require("org.kriscc.controlcenter.admin" not in actions,
        "generic admin Polkit action must not remain")
for action_id, node in actions.items():
    active = node.find("./defaults/allow_active")
    require(active is not None and (active.text or "") != "auth_admin_keep",
            f"{action_id}: retained authorization is forbidden")
    annotations = {n.attrib.get("key"): n.text for n in node.findall("annotate")}
    if action_id != "org.kriscc.controlcenter.bootc.status":
        require(annotations.get("org.freedesktop.policykit.exec.path") == "/usr/libexec/kriscc/admin",
                f"{action_id}: mutation does not use constrained admin helper")
        require(bool(annotations.get("org.freedesktop.policykit.exec.argv1")),
                f"{action_id}: mutation lacks semantic argv1 restriction")

for qml in ROOT.glob("qml/**/*.qml"):
    qml_text = qml.read_text(encoding="utf-8")
    require("PolkitHelper" not in qml_text, f"{qml}: PolkitHelper leaked into QML")
    require(not re.search(r'/(?:usr/)?bin/(?:bootc|dnf5|efibootmgr|grub2-reboot)', qml_text),
            f"{qml}: privileged executable leaked into QML")

require("Layout.preferredHeight: contentHeight" not in software_qml,
        "Software list virtualization regressed")
require("Layout.preferredHeight: contentHeight" not in flatpak_qml,
        "Flatpak list virtualization regressed")
require("Kirigami.ScrollablePage" not in software_qml
        and "Kirigami.ScrollablePage" not in flatpak_qml,
        "Software/Flatpak must have a single scrolling owner")
require("Layout.fillHeight: true" in software_qml
        and "Layout.fillHeight: true" in flatpak_qml,
        "Software/Flatpak list viewport must fill available height")
require("Novità repository" not in software_qml,
        "removed repository-news tab returned")
require('text: qsTr("Dettagli tecnici")' not in system_qml,
        "raw BootC JSON toggle returned")
require("id: flatpakDialog" not in system_qml
        and 'text: qsTr("Aggiorna tutto")' not in system_qml,
        "System updates tab must not duplicate Flatpak updating")
require('text: qsTr("Plasma")' not in system_qml,
        "System tools must not duplicate the Plasma launcher block")
require('launchTool("isoimagewriter")' in system_qml
        and 'QStringLiteral("isoimagewriter")' in system_cpp,
        "ISO Image Writer shortcut contract is missing")
require("id: toolsGrid" in system_qml
        and "Layout.columnSpan: toolsGrid.columns" in system_qml,
        "System tools Storage card must span the complete grid width")
require("entries.size() >= 500" not in package_cpp,
        "installed RPM inventory is silently capped")
require("entries.size() >= 100" in package_cpp and "m_truncated" in package_cpp,
        "RPM live-search result cap is not explicit")
require("rpmdb.sqlite" not in package_cpp,
        "PackageSearch must not hardcode an RPM database path")
require('QStringLiteral("firewalld.service")' in system_cpp,
        "Dashboard firewall state is not sourced from firewalld")
require("topMemoryProcesses" in read("src/SystemBackend.h"),
        "Dashboard top-memory model is missing")
require("std::min<qsizetype>(10, entries.size())" in system_cpp,
        "Dashboard must expose the top ten RAM process groups")
require('model: ["overlay", "sync", "selinux", "firewall", "storage", "network"]' in dashboard_qml,
        "Dashboard six-tile status grid order changed")
require('{ id: "system", title: qsTr("Sistema")' not in dashboard_qml
        and dashboard_qml.find('{ id: "flatpak", title: qsTr("Flatpak")')
            < dashboard_qml.find('{ id: "software", title: qsTr("Software")'),
        "Dashboard top row must start with Flatpak/Software and omit the redundant System tile")
require('qsTr("Prestazioni")' in dashboard_qml
        and 'qsTr("CPU")' in dashboard_qml
        and 'qsTr("Temperatura")' in dashboard_qml
        and 'qsTr("RAM")' in dashboard_qml
        and 'Layout.column: 2' in dashboard_qml
        and 'Layout.row: 0' in dashboard_qml
        and 'Layout.rowSpan: 4' in dashboard_qml,
        "Dashboard performance card must fill the complete third column")
require("memoryTotalMiB" not in dashboard_qml,
        "Dashboard RAM card must not show total/swap text")
require("networkState" in read("src/SystemBackend.h")
        and '"network"' in dashboard_qml,
        "Dashboard network card contract is missing")
require('SystemBackend.launchTool("kfind")' in dashboard_qml
        and "SystemBackend.openTemporaryFolder()" in dashboard_qml
        and "SystemBackend.openHomeFolder()" in dashboard_qml
        and "SystemBackend.openRootFolder()" in dashboard_qml,
        "Dashboard quick actions lost KFind, temporary folder, Home or root filesystem")
require('qsTr("Backup e Recovery")' not in dashboard_qml,
        "Dashboard quick actions must not duplicate Backup and Recovery")
require('qsTr("Terminale")' in dashboard_qml
        and 'columns: width >= 900 ? 6' in dashboard_qml
        and 'uniformCellWidths: true' in dashboard_qml,
        "Dashboard quick actions must remain six equal-width buttons on wide layouts")
require('"podman", title: qsTr("Container")' not in dashboard_qml,
        "Dashboard must not duplicate the Container navigation tile")
require("Aggiorna Control Center" not in dashboard_qml
        and "checkControlCenterUpdate()" in commands_qml
        and 'openRequested("system")' in commands_qml
        and "updateControlCenter()" not in commands_qml,
        "Control Center release check must stay in Commands and route updates through KrisOS")
require('QStringLiteral("--columns=name,url")' in utility_cpp,
        "Flatpak remotes must use the minimal name/url contract")
require("parseFlatpakTsv(message.toUtf8(), 2)" in utility_cpp,
        "Flatpak remote output is not parsed as the fixed two-column contract")
require("parseFlatpakRemotes" not in contract_parsers_h
        and "parseFlatpakRemotes" not in contract_parsers_cpp
        and "parseFlatpakRemotes" not in contract_parsers_test,
        "dead legacy Flatpak remote parser returned")
require('QStringLiteral("flatpak.info")' in utility_cpp
        and 'QStringLiteral("remote-info")' in utility_cpp
        and 'text: qsTr("Info")' in flatpak_qml,
        "Flatpak search results lost on-demand remote information")
require("Layout.alignment: Qt.AlignVCenter" in flatpak_qml,
        "Flatpak result icon is not vertically centered")
require("(?:rpm|i686|x86_64|noarch)" in recovery_qml,
        "Recovery forget input must reject package suffixes rejected by Validators::packageName")
require("anchors.right: parent.right" in read("qml/Main.qml")
        and "id: versionLabel" in read("qml/Main.qml"),
        "Version label is not anchored to the physical right edge")
require('text: qsTr("Info Center")' in read("qml/Main.qml")
        and read("qml/Main.qml").find('text: qsTr("Info Center")')
            < read("qml/Main.qml").find('text: qsTr("Impostazioni Plasma")'),
        "Info Center must stay in the fixed sidebar above Plasma settings")
require("columns: 3" in dashboard_qml
        and "Layout.rowSpan: 4" in dashboard_qml
        and "Layout.column: 2" in dashboard_qml,
        "Dashboard central area must use one aligned three-column grid without a top-right gap")
require('Home + partizione sistema' not in dashboard_qml,
        "Dashboard storage card must not add a redundant storage subtitle")
require('QStringLiteral("/sysroot")' in system_cpp
        and 'tr("Home: %1")' in system_cpp
        and 'tr("Sistema: %1")' in system_cpp,
        "Dashboard storage tile must report both Home and system filesystem")
require("Layout.maximumWidth: Layout.preferredWidth" in read("qml/modules/SoftwareModule.qml"),
        "Repository status/action columns are not fixed-width aligned")
require("else if (tabs.currentIndex === 2) upgradesModel.loadUpgrades()" in software_qml
        and "Component.onCompleted: upgradesModel.loadUpgrades()" not in software_qml,
        "Software upgrade inventory must stay lazy until the Aggiornabili tab is opened")
require(re.search(r'requestAction\(\s*"repo-enable"', software_qml) is not None
        and 'if (action === "repo-enable")' in software_qml,
        "repository enable mutation must require the shared confirmation flow")
require("backupDirectory" in read("src/SystemBackend.h")
        and "setBackupDirectory" in system_cpp,
        "selectable backup destination is missing")
require('qsTr("Escluso: ")' not in recovery_qml
        and 'qsTr("presente")' not in recovery_qml
        and 'qsTr("assente")' not in recovery_qml,
        "Backup preview must show only entries that are actually included")
require('item.insert(QStringLiteral("included"), false)' not in system_cpp
        and 'item.insert(QStringLiteral("exists")' not in system_cpp,
        "Backup preview backend must not expose excluded or absent rows")
require("partialFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)" in system_cpp,
        "backup partial file must be created as 0600")
require("JSON.parse(" not in read("qml/modules/PodmanModule.qml"),
        "Podman JSON parsing must stay in C++")
require('split("\\n")' not in read("qml/modules/FlatpakModule.qml"),
        "Flatpak TSV parsing must stay in C++")

wrapper = read("src/bootc-status.sh")
require('exec /usr/bin/timeout --signal=TERM --kill-after=3s 30s /usr/bin/bootc status --format json --format-version=1' in wrapper,
        "BootC JSON wrapper is not root-side bounded or no longer pins schema v1")
require('"$@"' not in wrapper, "BootC wrapper accepts arbitrary arguments")

for ignored in ("stage/", "artifacts/", "audit-build/", "*.rpm"):
    require(ignored in read(".gitignore"), f".gitignore missing {ignored}")

require("0.6 è" not in read("INTEGRAZIONE.md"), "integration docs are stale")
require("release 0.5.1" not in read("i18n/README.md"), "i18n docs are stale")
require("auth_admin_keep" not in read("data/org.kriscc.controlcenter.policy"),
        "Polkit retention is forbidden")

print(f"release audit OK: krisCC {VERSION}")
