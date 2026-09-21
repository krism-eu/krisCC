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
m_r = re.search(r'KRISCC_RELEASE\s+"([^"]+)"', version_cfg)
require(m_v and m_r, "missing canonical krisCC version")
VERSION, RELEASE = m_v.group(1), m_r.group(1)

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

require(f'Version:        {VERSION}' in spec, "RPM Version differs from canonical version")
require(f'Release:        {RELEASE}%{{?dist}}' in spec, "RPM Release differs from canonical release")
require('KRISCC_VERSION="${PROJECT_VERSION}-${KRISCC_RELEASE}"' in cmake,
        "UI version is not derived from canonical build version")
require(f'krisCC-{VERSION}-{RELEASE}.fc44.x86_64.rpm' in workflow,
        "CI artifact identity differs from canonical version")
require(f'<release version="{VERSION}"' in read("data/org.kriscc.KrisCC.metainfo.xml"),
        "AppStream release is stale")

require("src/Validators.cpp src/Validators.h" in cmake, "shared validators not linked")
require("src/AdminPolicy.cpp src/AdminPolicy.h" in cmake, "shared admin policy not linked")
require("kriscc-test-validators" in cmake and "kriscc-test-admin-policy" in cmake,
        "semantic unit tests are not wired into CTest")
require("AdminPolicy::resolve" in admin_cpp and "AdminPolicy::resolve" in polkit_cpp,
        "client/root privileged allowlist does not share AdminPolicy")
require('QStringLiteral("/usr/bin/rk")' in admin_policy
        and 'QStringLiteral("/usr/bin/bootc")' in admin_policy
        and 'QStringLiteral("/usr/bin/dnf5")' in admin_policy,
        "AdminPolicy lost fixed executable mapping")
require("/usr/bin/bash" not in admin_policy and "/usr/bin/sh" not in admin_policy,
        "AdminPolicy must never expose a root shell")
require("setStandardInputFile(QProcess::nullDevice())" in admin_cpp,
        "root helper stdin must be closed")
require("SIGTERM" in admin_cpp and "SIGKILL" in admin_cpp and "return 124" in admin_cpp,
        "root helper timeout is not real/bounded")
require('m_polkit->execute(QStringLiteral("/usr/libexec/kriscc/admin")' in rk_cpp,
        "krisCC rk mutations must pass through the supervised admin helper")
require('m_polkit->execute(QStringLiteral("/usr/bin/rk")' not in rk_cpp,
        "krisCC must not launch privileged rk directly")
require("setStandardInputFile(QProcess::nullDevice())" in custom_cpp,
        "personal commands must not inherit interactive stdin")
require("setStandardInputFile(QProcess::nullDevice())" in utility_cpp,
        "utility commands must not inherit interactive stdin")

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
require("Novità repository" not in software_qml,
        "removed repository-news tab returned")
require('text: qsTr("Dettagli tecnici")' not in system_qml,
        "raw BootC JSON toggle returned")
require("entries.size() >= 500" not in package_cpp,
        "installed RPM inventory is silently capped")
require("entries.size() >= 100" in package_cpp and "m_truncated" in package_cpp,
        "RPM live-search result cap is not explicit")
require('QStringLiteral("/usr/share/rpm/rpmdb.sqlite")' in package_cpp,
        "Fedora 44 RPM DB cache path is missing")
require('QStringLiteral("firewalld.service")' in system_cpp,
        "Dashboard firewall state is not sourced from firewalld")
require("topMemoryProcesses" in read("src/SystemBackend.h"),
        "Dashboard top-memory model is missing")

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

print(f"release audit OK: krisCC {VERSION}-{RELEASE}")