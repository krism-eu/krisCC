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
require(m_v is not None, "missing canonical krisCC version")
VERSION = m_v.group(1)
require(re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", VERSION) is not None,
        "public krisCC version must be X.Y.Z only")
require("KRISCC_RELEASE" not in version_cfg,
        "RPM release must not be part of the public version source")

cmake = read("CMakeLists.txt")
spec = read("packaging/krisCC.spec")
workflow = read(".github/workflows/build.yml")
promote_workflow = read(".github/workflows/promote-stable.yml")
main_cpp = read("src/main.cpp")
polkit_cpp = read("src/PolkitHelper.cpp")
admin_cpp = read("src/AdminHelper.cpp")
admin_policy = read("src/AdminPolicy.cpp")
rk_cpp = read("src/RkBackend.cpp")
custom_cpp = read("src/CustomActionsBackend.cpp")
cron_cpp = read("src/CronBackend.cpp")
cron_parser_cpp = read("src/CronParser.cpp")
utility_cpp = read("src/UtilityBackend.cpp")
package_cpp = read("src/PackageSearch.cpp")
package_h = read("src/PackageSearch.h")
package_inventory_cpp = read("src/PackageInventoryCache.cpp")
package_inventory_h = read("src/PackageInventoryCache.h")
package_inventory_test = read("tests/test_package_inventory.cpp")
system_cpp = read("src/SystemBackend.cpp")
system_h = read("src/SystemBackend.h")
runtime_h = read("src/SystemBackendRuntime.h")
runtime_lifecycle = read("src/SystemBackendRuntimeLifecycle.cpp")
runtime_services = read("src/SystemBackendRuntimeServices.cpp")
runtime_boot = read("src/SystemBackendRuntimeBoot.cpp")
process_runner = read("src/ProcessRunner.cpp")
service_manager_cpp = read("src/ServiceManagerBackend.cpp")
service_json_cpp = read("src/ServiceJson.cpp")
systemd_jobs = read("src/SystemdJobCoordinator.cpp")
maintenance_trash = read("src/MaintenanceTrash.cpp")
cleanup_estimate = read("src/cleanup-estimate.sh")
software_qml = read("qml/modules/SoftwareModule.qml")
system_qml = read("qml/modules/SystemModule.qml")
dashboard_qml = read("qml/modules/DashboardModule.qml")
main_qml = read("qml/Main.qml")
commands_qml = read("qml/modules/CommandsModule.qml")
tools_qml = read("qml/modules/ToolsModule.qml")
services_qml = read("qml/modules/ServicesNetworkModule.qml")
repair_cpp = read("src/RepairBackend.cpp")
recovery_qml = read("qml/modules/RecoveryModule.qml")
contract_parsers_h = read("src/ContractParsers.h")
contract_parsers_cpp = read("src/ContractParsers.cpp")
contract_parsers_test = read("tests/test_contract_parsers.cpp")
cron_parser_test = read("tests/test_cron_parser.cpp")

# Version/release identity and Fedora 45 build contract.
require(f'Version:        {VERSION}' in spec, "RPM Version differs from canonical version")
require('Release:        1%{?dist}' in spec,
        "RPM Release must stay fixed at 1; bump X.Y.Z instead")
runtime_paths = (
    "/usr/bin/timeout",
    "/usr/bin/systemctl",
    "/usr/bin/loginctl",
    "/usr/bin/resolvectl",
    "/usr/bin/journalctl",
)
require(all(f"Requires:       {path}" in spec for path in runtime_paths)
        and 'Requires:       coreutils' not in spec
        and 'Requires:       systemd' not in spec,
        "Fedora 45 runtime contract must use executable-path requirements")
require('%{_libexecdir}/kriscc/cleanup-estimate' in spec
        and 'Requires:       libarchive' not in spec
        and 'Requires:       tar' not in spec
        and '%{_libexecdir}/kriscc/archive' not in spec,
        "retired personal-backup runtime dependencies/helper are still packaged")
require('KRISCC_VERSION="${PROJECT_VERSION}"' in cmake,
        "UI version must be exactly canonical X.Y.Z")
require('KrisCCVersion.cmake' in workflow
        and 'release="1"' in workflow
        and 'vr="${version}-${release}"' in workflow
        and 'echo "rpm=krisCC-${vr}.fc45.x86_64.rpm"' in workflow
        and workflow.count("container: fedora:45") == 3
        and "container: fedora:44" not in workflow,
        "CI RPM identity is not derived from canonical X.Y.Z with fixed Release 1")
require('echo "source_txt=krisCC-${vr}-source.txt"' in workflow,
        "CI source TXT identity is not derived from canonical version+release")
require('source_zip' not in workflow.lower()
        and '-source.zip' not in workflow.lower()
        and 'source_zip' not in promote_workflow.lower()
        and '-source.zip' not in promote_workflow.lower(),
        "retired repository source ZIP is still generated, published, or promoted")
require('VERSION: ${{ steps.identity.outputs.version }}' in workflow
        and '"krisCC ${VERSION}"' in workflow,
        "CI does not verify the public X.Y.Z application version")
require('fetch-depth: 1' in workflow
        and 'python3 tools/source_snapshot.py' in workflow
        and 'git archive' not in workflow
        and '--format=zip' not in workflow,
        "CI exact-source TXT snapshot contract is incomplete or source ZIP generation returned")
require('sha256sum "$RPM" "$SOURCE_TXT" > SHA256SUMS' in workflow
        and 'test "$(wc -l < SHA256SUMS)" -eq 2' in workflow,
        "RPM/source TXT are not covered by one two-entry SHA256SUMS")
require(f'<release version="{VERSION}"' in read("data/org.kriscc.KrisCC.metainfo.xml"),
        "AppStream release is stale")

source_snapshot = read("tools/source_snapshot.py")
require('run_git(root, "ls-tree", "-r", "-z", "--full-tree", commit)' in source_snapshot
        and 'run_git(root, "cat-file", "blob", object_sha)' in source_snapshot
        and '# commit:' in source_snapshot
        and '# tree:' in source_snapshot
        and 'hashlib.sha256' in source_snapshot,
        "source TXT generator does not snapshot and fingerprint the exact tracked Git tree")

# Active runtime supplies the abstract base operations.
require('SystemBackendRuntime systemBackend(&polkitHelper);' in main_cpp,
        "main does not instantiate the hardened SystemBackend runtime")
for source in (
    "src/SystemBackendRuntime.h", "src/SystemBackendRuntimeLifecycle.cpp",
    "src/SystemBackendRuntimeServices.cpp", "src/SystemBackendRuntimeBoot.cpp",
    "src/SystemdJobCoordinator.cpp",
    "src/ServiceJson.cpp", "src/PackageInventoryCache.cpp",
):
    require(source in cmake, f"active hardening source not linked: {source}")

# Unit/regression coverage is part of the release contract.
for target in (
    "kriscc-test-validators", "kriscc-test-admin-policy", "kriscc-test-process-runner",
    "kriscc-test-service-json", "kriscc-test-system-backend-contract",
    "kriscc-test-service-manager-reentrancy", "kriscc-test-systemd-job-coordinator",
    "kriscc-test-maintenance-trash", "cleanup-estimate", "kriscc-test-package-inventory",
    "kriscc-test-parsers", "kriscc-test-cron-parser",
):
    require(target in cmake, f"required regression target is not wired into CTest: {target}")

# Privilege boundary: one constrained helper, no generic shell/admin path, no retained auth.
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
require('m_polkit->execute(QStringLiteral("/usr/libexec/kriscc/admin")' in rk_cpp
        and 'm_polkit->execute(QStringLiteral("/usr/bin/rk")' not in rk_cpp,
        "rk mutations must pass through the supervised admin helper")

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

uefi_actions = {
    "org.kriscc.controlcenter.boot.read-uefi": "boot-read-uefi",
    "org.kriscc.controlcenter.bootnext.clear-uefi": "boot-clear-next-uefi",
    "org.kriscc.controlcenter.boot.delete-uefi": "boot-delete-uefi",
    "org.kriscc.controlcenter.boot.order-uefi": "boot-order-uefi",
}
for action_id, operation in uefi_actions.items():
    require(action_id in actions, f"missing UEFI Polkit action: {action_id}")
    annotations = {n.attrib.get("key"): n.text for n in actions[action_id].findall("annotate")}
    require(annotations.get("org.freedesktop.policykit.exec.argv1") == operation,
            f"{action_id}: wrong semantic operation mapping")

for qml in ROOT.glob("qml/**/*.qml"):
    qml_text = qml.read_text(encoding="utf-8")
    require("PolkitHelper" not in qml_text, f"{qml}: PolkitHelper leaked into QML")
    require(not re.search(r'/(?:usr/)?bin/(?:bootc|dnf5|efibootmgr|grub2-reboot)', qml_text),
            f"{qml}: privileged executable leaked into QML")

# KR-04: group lifetime is independent from QProcess leader lifetime and PGID reuse is guarded.
require("setStandardInputFile(QProcess::nullDevice())" in process_runner,
        "shared user-level ProcessRunner must close stdin")
require("processesInGroup" in process_runner
        and "mapsShareIdentity" in process_runner
        and "m_processGroupId" in process_runner
        and "m_leaderExited" in process_runner
        and "trackedGroupAlive" in process_runner
        and "::kill(-m_processGroupId" in process_runner,
        "ProcessRunner does not track descendant identity across leader exit")
process_runner_test = read("tests/test_process_runner.cpp")
require("cancellationOutlivesLeaderAndKillsChild" in process_runner_test
        and "normalLeaderExitWaitsForDescendant" in process_runner_test,
        "ProcessRunner descendant regressions are missing")
require("ProcessRunner" in custom_cpp and "ProcessRunner" in utility_cpp,
        "custom actions and utility commands must use the shared ProcessRunner")

# Personal backups are delegated to Back In Time.
require('QStringLiteral("backintime"), QStringLiteral("backintime-qt")' in system_cpp
        and 'SystemBackend.toolAvailable("backintime")' in recovery_qml
        and 'SystemBackend.launchTool("backintime")' in recovery_qml,
        "personal backup is not delegated to Back In Time")
require("createSnapshot" not in system_h
        and "backupBusy" not in system_h
        and "kriscc-archive" not in cmake
        and "ArchiveRestoreEngine" not in cmake
        and "BackupSafety" not in cmake,
        "retired internal personal-backup engine is still exposed or built")

# KR-13: close/reboot coordinate with active mutations still owned by krisCC.
require("onClosing: function(close)" in main_qml
        and "mutationActive" in main_qml
        and "SystemBackend.mutationRunning" in main_qml,
        "window close does not coordinate with active mutations")
require("if (mutationRunning())" in runtime_lifecycle
        and "Riavvio rimandato" in runtime_lifecycle
        and "Riavvio nel firmware rimandato" in runtime_lifecycle,
        "reboot paths do not coordinate with active system mutations")

# KR-05/KR-12/KR-16: trash metadata order, single cleanup session, reliable estimates.
require("successfullyRemovedData" in maintenance_trash
        and maintenance_trash.index("successfullyRemovedData") < maintenance_trash.index(".trashinfo")
        and "dataEntryExists" in maintenance_trash,
        "trash metadata may be removed before corresponding data")
require("cleanupSessionActive" in tools_qml
        and "cleanupGeneration" in tools_qml
        and "finishCleanupStep" in tools_qml
        and "cleanupCurrent === id" in tools_qml
        and "cleanupWaiting" in tools_qml,
        "unified cleanup does not have one guarded session lifecycle")
require('/usr/libexec/kriscc/cleanup-estimate' in utility_cpp,
        "cleanup estimate is not routed through the dedicated bounded helper")
require('if out="$($DU_BIN -sh -- "$path" 2>/dev/null)"; then' in cleanup_estimate
        and "accesso negato" in cleanup_estimate
        and "non misurabile" in cleanup_estimate
        and cleanup_estimate.count("printf '") >= 4,
        "cleanup estimate does not distinguish producer errors/absence or complete each line")
cleanup_test = read("tests/test_cleanup_estimate.sh")
require("du-fail" in cleanup_test
        and "journal-fail" in cleanup_test
        and "Cestini: assente" in cleanup_test
        and "Journal: circa 0B" in cleanup_test
        and 'wc -l' in cleanup_test,
        "cleanup estimate behavioral regressions are incomplete")

# KR-06/07/08/09: reentrancy-safe service completion, union inventory, parse errors, real systemd jobs.
require("finish(state, message);\n            emit controlFinished" in service_manager_cpp
        and "finish(QStringLiteral(\"success\")" in service_manager_cpp,
        "service control state is not finalized before external completion signals")
require('list-units' in service_manager_cpp
        and 'list-unit-files' in service_manager_cpp
        and 'QStringLiteral("not-loaded")' in service_manager_cpp,
        "service inventory does not merge loaded and installed-only units")
require("ServiceJson::parseUnitList" in service_manager_cpp
        and "ServiceJson::parseUnitFiles" in service_manager_cpp
        and "Output JSON systemd troncato" in service_manager_cpp,
        "systemd JSON failure/truncation is not distinct from an empty healthy list")
require("QJsonParseError" in service_json_cpp and "document.isArray()" in service_json_cpp,
        "ServiceJson parser does not expose a strict structured contract")
require('QStringLiteral("Subscribe")' in systemd_jobs
        and "m_earlyResults" in systemd_jobs
        and "acceptJobPath" in systemd_jobs
        and "serviceOwnerChanged" in systemd_jobs
        and "Tempo massimo superato attendendo il job systemd" in systemd_jobs,
        "systemd mutations do not follow the real job lifecycle")
require("kriscc-test-service-manager-reentrancy" in cmake
        and "kriscc-test-systemd-job-coordinator" in cmake,
        "service/systemd behavioral race regressions are missing")

# KR-11: UEFI and GRUB readers retain independent lifecycles and notifications.
require("m_uefiProcess" in runtime_h and "m_grubProcess" in runtime_h
        and "m_uefiBusy" in runtime_h and "m_grubBusy" in runtime_h
        and "m_uefiRequestGeneration" in runtime_h and "m_grubRequestGeneration" in runtime_h,
        "UEFI and GRUB read lifecycles are still shared")
require("SystemBackendRuntime::refreshUefiEntries" in runtime_boot
        and "SystemBackendRuntime::refreshGrubEntries" in runtime_boot
        and "emit bootEntriesChanged();" in runtime_boot,
        "runtime boot readers are not independently implemented")

# KR-17/KR-19: shared invalidatable RPM/base/persistent snapshot; no false local classification on errors.
require("PackageInventoryCache::shared()" in package_cpp
        and "m_inventory->ensureFresh" in package_cpp
        and "PackageSearch::invalidateSharedInventory" in main_cpp,
        "PackageSearch models do not share/invalidate one inventory coordinator")
require('QStringLiteral("/usr/bin/rpm")' in package_inventory_h
        and 'QStringLiteral("/usr/share/krisos/owned-packages.txt")' in package_inventory_h
        and 'QStringLiteral("/var/lib/krisos/packages.list")' in package_inventory_h,
        "inventory coordinator does not own the three canonical sources")
require("ttlMs" in package_inventory_h
        and "expired()" in package_inventory_cpp
        and "m_forceAgain" in package_inventory_cpp
        and "requestEpoch != m_epoch" in package_inventory_cpp,
        "inventory cache lacks expiry or anti-stale invalidation")
require("QSet<QString> candidate" in package_inventory_cpp
        and "file.error() != QFileDevice::NoError" in package_inventory_cpp
        and "m_owned = std::move(owned)" in package_inventory_cpp
        and "m_persistent = std::move(persistent)" in package_inventory_cpp,
        "manifest refresh is not atomic/fail-closed")
require("Classificazione dei pacchetti non disponibile" in package_cpp
        and "!m_inventory->ready()" in package_cpp,
        "manifest/inventory errors can still publish false local/removable classifications")
require("rpmdb.sqlite" not in package_cpp and "rpmdb.sqlite" not in package_inventory_cpp,
        "package inventory must not hardcode the RPM database path")
for case in (
    "cacheIsReusedAndInvalidated", "missingOwnedManifestRecovers",
    "concurrentRequestsShareOneRpmLoad", "forcedRefreshPublishesMetadataAtomically",
    "failedRefreshNeverPublishesEmptyInventory",
):
    require(case in package_inventory_test, f"missing package-inventory regression: {case}")

# Existing architecture/ownership invariants preserved by the hardening.
command_ids = set(re.findall(r'\{\s*id:\s*"([^"]+)"', commands_qml))
bookmark_ids = set(re.findall(r'id == QStringLiteral\("([^"]+)"\)', utility_cpp))
require(command_ids <= bookmark_ids,
        "CommandsModule contains bookmark IDs not implemented by UtilityBackend: "
        + ", ".join(sorted(command_ids - bookmark_ids)))
require("Layout.preferredHeight: contentHeight" not in software_qml
        and "Kirigami.ScrollablePage" not in software_qml
        and "Layout.fillHeight: true" in software_qml,
        "Software list scrolling/virtualization regressed")
require("entries.size() >= 500" not in package_cpp
        and "entries.size() >= 100" in package_cpp
        and "m_truncated" in package_cpp,
        "RPM live-search cap/inventory behavior regressed")
require("qml/modules/FlatpakModule.qml" not in cmake
        and "runFlatpak" not in utility_cpp
        and "addFlathubUser" not in utility_cpp
        and "parseFlatpakTsv" not in contract_parsers_h
        and "parseFlatpakTsv" not in contract_parsers_cpp
        and "parseFlatpakTsv" not in contract_parsers_test,
        "Flatpak management engine must stay delegated to KDE Discover")
require("qml/modules/PodmanModule.qml" not in cmake
        and "runPodman" not in utility_cpp
        and "parsePodmanJson" not in contract_parsers_h,
        "Podman ownership must stay outside krisCC")
require('else if (pageId === "flatpak") SystemBackend.launchTool("discover")' in main_qml
        and '{ id: "flatpak", title: qsTr("Flatpak")' in dashboard_qml,
        "Dashboard Flatpak entry must delegate to KDE Discover")
require("topMemoryProcesses" in system_h
        and "std::min<qsizetype>(10, entries.size())" in system_cpp,
        "Dashboard top-memory model regressed")
require('QStringLiteral("firewalld.service")' in system_cpp
        and 'QStringLiteral("cockpit.socket")' in system_cpp,
        "Dashboard common-service state sources regressed")
require('QStringLiteral("wifi")' in system_cpp and 'WirelessEnabled' in system_cpp,
        "Wi-Fi common-service state must use NetworkManager radio state")
require('selectedInterface = iface.name();' in system_cpp
        and 'selectedInterface = iface.humanReadableName()' not in system_cpp,
        "NetworkManager mutations must use the kernel interface name")
require('networkDisplayName' in system_h and 'iface.humanReadableName()' in system_cpp,
        "network UI must keep display label separate from kernel interface name")
require("OperationLog::append" in repair_cpp,
        "RepairBackend mutations must be recorded in operation history")
require("m_operationLines.isEmpty()" in rk_cpp
        and "m_operationLines.append(output.trimmed())" in rk_cpp,
        "rk privileged failures must surface a fallback operation line")
require("pendingPreviewPackage" in software_qml
        and 'utilityBackend.operationId === "rpm.plan"' in software_qml
        and "utilityBackend.cancel()" in software_qml,
        "rk plan preview cancellation/serialization contract is missing")
require('parseDnfListJson("{}")' in contract_parsers_test,
        "empty DNF5 list JSON regression test is missing")
require('restartAudio()' in read("src/RepairBackend.h")
        and 'flushDns()' in read("src/RepairBackend.h")
        and 'reconnectNetwork' in repair_cpp,
        "operational audio/network repair contract is incomplete")
require('QStringLiteral("--vacuum-size=16M")' in admin_policy
        and '--vacuum-size=100M' not in admin_policy
        and 'Journal archiviati oltre 16 MiB' in tools_qml,
        "journal vacuum must remain bounded at 16 MiB")
require('id: cleanupConfirmDialog' in tools_qml
        and 'onClicked: cleanupConfirmDialog.open()' in tools_qml
        and 'La pulizia dei cestini è irreversibile.' in tools_qml,
        "unified cleanup must require explicit confirmation")

# Cron and fixed command viewer remain read-only/capability-driven.
require("src/CronBackend.cpp src/CronBackend.h" in cmake
        and "src/CronParser.cpp src/CronParser.h" in cmake,
        "cron viewer backend/parser not linked")
require('QStringLiteral("-l")' in cron_cpp
        and 'QIODevice::ReadOnly' in cron_cpp
        and 'QStringLiteral("/etc/crontab")' in cron_cpp
        and 'QStringLiteral("/etc/cron.d")' in cron_cpp
        and "pkexec" not in cron_cpp
        and "Polkit" not in cron_cpp
        and "crontab -e" not in cron_cpp,
        "cron viewer must remain capability-driven and read-only")
require("parsesUserCrontab" in cron_parser_test
        and "parsesSystemCrontab" in cron_parser_test
        and "describesCommonSchedules" in cron_parser_test
        and "QRegularExpression" in cron_parser_cpp,
        "cron parser contract/tests are incomplete")

# Boot/update ownership and version UI remain image-owned.
require("Aggiorna Control Center" not in dashboard_qml
        and "checkControlCenterUpdate()" in system_qml
        and "checkControlCenterUpdate()" not in commands_qml
        and "updateControlCenter()" not in commands_qml
        and "fc44.x86_64.rpm" not in system_cpp
        and "fc45.x86_64.rpm" not in system_cpp,
        "Control Center release check must stay image-owned")
require("QTimer::singleShot(15000, reply" in system_cpp,
        "Control Center update check must have a network timeout")
require('label: qsTr("Comandi")' in main_qml
        and 'label: qsTr("Strumenti & Fix")' in main_qml
        and main_qml.index('label: qsTr("Comandi")') < main_qml.index('label: qsTr("Strumenti & Fix")'),
        "navigation must keep Commands before Tools & Fix")
require('text: qsTr("Terminale")' in main_qml
        and 'text: qsTr("Info Center")' in main_qml,
        "persistent sidebar tools regressed")
require('anchors.right: parent.right' in main_qml and 'id: versionLabel' in main_qml,
        "version label is not anchored to the physical right edge")

# Integration docs and wrappers remain pinned/read-only.
wrapper = read("src/bootc-status.sh")
require('exec /usr/bin/timeout --signal=TERM --kill-after=3s 30s /usr/bin/bootc status --format json --format-version=1' in wrapper,
        "BootC JSON wrapper is not bounded or no longer pins schema v1")
require('"$@"' not in wrapper, "BootC wrapper accepts arbitrary arguments")
for ignored in ("stage/", "artifacts/", "audit-build/", "*.rpm"):
    require(ignored in read(".gitignore"), f".gitignore missing {ignored}")
require("--output=json" in read("INTEGRAZIONE.md")
        and "list-units" in read("INTEGRAZIONE.md")
        and "list-unit-files" in read("INTEGRAZIONE.md")
        and "Back In Time" in read("INTEGRAZIONE.md")
        and "fotografia coerente" in read("INTEGRAZIONE.md"),
        "integration docs do not state current structured runtime/inventory contracts")
require("auth_admin_keep" not in read("data/org.kriscc.controlcenter.policy"),
        "Polkit retention is forbidden")

print(f"release audit OK: krisCC {VERSION}")