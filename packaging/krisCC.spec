Name:           krisCC
Version:        0.7.9
Release:        1%{?dist}
Summary:        krisCC personal control center for KrisOS and Fedora bootc
License:        MIT
URL:            https://github.com/krism-eu/krisCC
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc-c++
BuildRequires:  cmake
BuildRequires:  ninja-build
BuildRequires:  qt6-qtbase-devel
BuildRequires:  qt6-qtdeclarative-devel
BuildRequires:  kf6-kirigami-devel

Requires:       qt6-qtbase
Requires:       qt6-qtdeclarative
Requires:       kf6-kirigami
Requires:       polkit
Requires:       rpm
Requires:       dnf5
Requires:       dnf5-plugins
Requires:       NetworkManager
Requires:       bootc
Requires:       tar
Requires:       bash

%description
krisCC is a compact personal Kirigami control center for KrisOS and Fedora bootc
systems. It focuses on persistent KrisOS software management, operational repair
tools, services/network controls, bootc updates, diagnostics, recovery and local
configuration/home backups without duplicating dedicated application managers.
Flatpak application management is delegated to KDE Discover and containers are
delegated to a dedicated external application.

%prep
%autosetup -n %{name}-%{version}

%build
%cmake -G Ninja
%cmake_build

%install
%cmake_install

%files
%license LICENSE
%doc README.md
%{_bindir}/krisCC
%{_libexecdir}/kriscc/admin
%{_libexecdir}/kriscc/bootc-status
%{_libexecdir}/kriscc/maintenance
%{_datadir}/applications/krisCC.desktop
%{_datadir}/polkit-1/actions/org.kriscc.controlcenter.policy
%{_datadir}/metainfo/org.kriscc.KrisCC.metainfo.xml
%{_datadir}/icons/hicolor/scalable/apps/krisCC.svg

%changelog
* Thu Sep 24 2026 krism-eu - 0.7.9-1
- Final dashboard top status strip with EFI, Cockpit state and refresh tiles
- Make Wi-Fi radio controllable through NetworkManager even when disabled
- Manage cockpit.socket explicitly from Services & Network without opening a browser
- Compact diagnostics/tools layout and extend fixed read-only command bookmarks
- Keep six Dashboard quick-action slots with one reserved placeholder

* Thu Sep 24 2026 krism-eu - 0.7.8-1
- Finalize dashboard/sidebar and System & Boot layout without changing Avvio e dischi
- Consolidate repair, cleanup and diagnostics under Strumenti & Fix
- Simplify Services & Network and expose public IP / active DNS inspection
- Add informative next-EFI-boot tile and bounded operation history
- Harden Cockpit on-demand activation, service feedback and backup/restore contracts
- Reduce archived journal vacuum target to 16 MiB

* Thu Sep 24 2026 krism-eu - 0.7.7-1
- Recast krisCC as an operational Swiss Army control center with Tools & Fix and Services & Network
- Remove internal Podman/container management completely
- Add audio repair, DNS flush/presets, active-network reapply and NetworkManager handoff
- Add unified sequential cleanup for trash, journal, DNF cache and unused Flatpak runtimes with estimates
- Add allowlisted service start/stop/restart/reset controls and failed-unit diagnostics
- Add firmware-setup reboot, read-only kernel arguments, VA-API/GPU diagnostics and Cockpit shortcut

* Thu Sep 24 2026 krism-eu - 0.7.6-1
- Delegate Flatpak application management to KDE Discover and remove the duplicate engine/page
- Fix Podman image deletion, empty DNF5 upgrade lists and rk authentication error feedback
- Serialize rk plan previews so stale solver output cannot cross package dialogs
- Exclude the active backup archive when the selected backup directory is Home
- Keep the Dashboard Flatpak tile as the Discover entry point and simplify the stable release check

* Wed Sep 23 2026 krism-eu - 0.7.5-1
- Preserve KrisOS image-owned krisCC semantics and route updates through the OS image
- Remove the dead Flatpak remote parser and align recovery input validation
- Streamline Dashboard and System layouts and add on-demand Flatpak details
- Keep Software upgrade enumeration lazy until the Aggiornabili tab is opened
- Align the Dashboard on one three-column grid with combined CPU/temperature and dual storage status
- Use six uniform quick actions and refresh the application icon
- Finalize the Dashboard performance column and simplify backup previews to included content only

* Wed Sep 23 2026 krism-eu - 0.7.4-1
- Fail closed on incomplete tar backups and preflight archives before restore
- Bound backup create, verify and restore operations with process-group timeouts
- Require confirmation before re-enabling DNF repositories
- Rebalance the dashboard around RAM plus an eight-tile status grid and top-ten process view
- Add KFind and temporary-folder shortcuts and a constrained stable krisCC updater in Commands

* Tue Sep 22 2026 krism-eu - 0.7.3-1
- Export exact latest-green repository commit from GitHub ZIP
- Simplify Flatpak remote contract to name/url
- Keep network status card in the 0.7 dashboard

* Tue Sep 22 2026 krism-eu - 0.7.2-1
- Adopt public X.Y.Z-only versioning; RPM Release remains packaging metadata
- Preserve the last green 0.7 trial behavior unchanged

* Mon Sep 21 2026 krism-eu - 0.7.1-5
- Make downloaded BootC apply-and-reboot match the UI contract
- Normalize sidebar page indices and fail closed on unexpected rk verbs
- Reduce top-process sampling cost and harden DNF summary parsing
- Fix overlay-relative dialog sizing, background diagnostics and bash dependency
- Verify Fedora GNU tar traversal behavior in CI

* Mon Sep 21 2026 krism-eu - 0.7.1-4
- Keep Flatpak and Podman machine-readable stdout isolated from diagnostic stderr
- Add regression coverage for structured stdout accompanied by stderr warnings

* Mon Sep 21 2026 krism-eu - 0.7.1-3
- Accept the real Flatpak remotes contract while keeping strict parsing elsewhere
- Fill the Dashboard health grid with live interface/IP state and a colored connection indicator
- Export any active public krisCC/KrisOS branch to one owner-private text file in Home
- Remove the remaining duplicate Commands page heading

* Mon Sep 21 2026 krism-eu - 0.7.1-2
- Fix header/repository/dashboard alignment found by reference-host acceptance
- Preserve Flatpak structured output before parsing empty trailing columns
- Make top-memory process collection resilient to partial /proc visibility

* Mon Sep 21 2026 krism-eu - 0.7.1-1
- Harden privileged execution with root-owned timeouts and shared allowlists
- Refresh the dashboard, virtualize RPM and Flatpak lists, and remove silent list truncation
- Add firewall, top-memory process visibility, Flatpak launch, Podman container removal and backup deletion
- Remove duplicate page headers and unreadable raw BootC status from the normal UI

* Sun Sep 20 2026 krism-eu - 0.7.0-6
- Fix resource sampling and load pages on demand
- Unify native cards, output panels and responsive tools
- Add confirmed user trash cleanup to Dashboard

* Sun Sep 20 2026 krism-eu - 0.7.0-5
- Replace the horizontal tab shell with a KDE-style sidebar and branded header
- Rework the dashboard around live KrisOS status, recent operations and quick actions
- Keep the 0.7 backend contracts unchanged while presenting real runtime data

* Sun Sep 20 2026 krism-eu - 0.7.0-4
- Fail loudly when DNF5 machine-readable output no longer matches the expected contract
- Refresh core systemd service state asynchronously instead of blocking the QML thread
- Declare Qt 6.7 as the actual minimum required by the D-Bus authorization API

* Sun Sep 20 2026 krism-eu - 0.7.0-3
- Harden privileged mutations behind one validated admin helper
- Run trash cleanup without root privileges and bound privileged processes
- Make personal scripts fail closed, private, and cancellable as process groups
- Suspend dashboard hardware polling while hidden and harden logs and D-Bus authorization

* Sun Sep 20 2026 krism-eu - 0.7.0-2
- Add persistent per-user custom commands and multiline scripts
- Show CPU usage, RAM used without swap and CPU temperature on the dashboard
- Move privileged BootC, RK, repository and next-boot actions behind typed backends
- Consume the versioned KrisOS rk status JSON contract and remove Raku compatibility paths

* Sun Sep 20 2026 krism-eu - 0.7.0-1
- Introduce structured RK and maintenance backends with tighter privilege boundaries
- Refresh the Plasma/Kirigami dashboard and daily diagnostics around typed system state
- Establish the long-term architecture contract and stable promotion workflow

* Sun Sep 20 2026 krism-eu - 0.6.0-2
- Add structured rk recovery state shared by Dashboard and Recovery
- Add tightly allowlisted root trash maintenance for home and mounted volumes
- Refocus command bookmarks on daily diagnostics and harden the release contract

* Sat Sep 19 2026 krism-eu - 0.6.0-1
- Make BootC staged-update actions match download-only and ready-to-boot states
- Pin the BootC JSON v1 contract and validate supported flags in CI
- Bound user mutations, reject credential-bearing repository URLs and stabilize RPM search results

* Sat Sep 19 2026 krism-eu - 0.5.1-10
- Keep all seven pages alive and isolate utility operations per page
- Complete BootC staged/progress/refresh UX and harden dialogs and confirmations
- Fix Flatpak remote handling, RPM filtering, Podman formatting and final K1.0 readability

* Fri Sep 18 2026 krism-eu - 0.5.1-9
- Harden repository management to HTTPS-only and align it with the KrisOS rk gate
- Separate read-only bootc status behind a fixed Polkit wrapper
- Exclude rebuildable Flatpak/Podman stores from home backups and tighten Flatpak user scope

* Fri Sep 18 2026 krism-eu - 0.5.1-8
- K1.0 restyle integration candidate with complete UI review and validated repository/container/backup workflows
- Keep 0.5.1-7 on main unchanged as the stable backup component

* Fri Sep 18 2026 krism-eu - 0.5.1-7
- Final validated 0.5.1 runtime release with no application-behavior changes
- Supersede 0.5.1-6 with the same audited feature set and release gates

* Fri Sep 18 2026 krism-eu - 0.5.1-6
- Publish runtime-only release checksums and verify the exact prerelease artifact on promotion
- Supersede the 0.5.1-5 release-pipeline candidate without changing application behavior

* Fri Sep 18 2026 krism-eu - 0.5.1-5
- Promote the exact integration-tested RPM when the candidate source tree matches main
- Avoid treating independent RPM rebuilds as byte-reproducible release artifacts

* Fri Sep 18 2026 krism-eu - 0.5.1-4
- Optimize backend state accessors without changing the QML architecture
- Remove the unused duplicate rkStatus API after strict release audit

* Fri Sep 18 2026 krism-eu - 0.5.1-3
- Correct Fedora 44 usr-merged paths for efibootmgr, grubby and grub2-reboot
- Supersede the pre-audit 0.5.1-2 candidate

* Fri Sep 18 2026 krism-eu - 0.5.1-2
- Harden backup process failure handling and keep verification output bounded
- Keep Flatpak cleanup in user scope and avoid duplicate QML page recreation
- Tighten one-shot GRUB argument validation
- Add cross-layer release audit and strict lint/static-analysis gates

* Fri Sep 18 2026 krism-eu - 0.5.1-1
- Integration release for the complete KrisOS ISO
- Keep the validated 0.5 feature set unchanged

* Fri Sep 18 2026 krism-eu - 0.5.0-1
- Add a unified minimal KrisOS system/update center
- Add health, security, storage, UEFI and GRUB/BLS read-only diagnostics
- Support one-shot UEFI BootNext and GRUB next-entry selection when available
- Add backup inventory, verification and user-level restore
- Add local operation history and expand safe terminal command bookmarks
- Keep firmware and first-run/welcome tooling out of krisCC

* Thu Sep 17 2026 krism-eu - 0.4.0-11
- Use structured backend operation identifiers and result states in QML
- Make backup archives atomic with warning/cancellation handling
- Make --background a single D-Bus-activatable instance
- Test the installed RPM artifact in CI

* Thu Sep 17 2026 krism-eu - 0.4.0-10
- Align BootC controls with KrisOS single-deployment update model
- Route RPM previews through rk policy and limit discovery to supported repositories
- Remove privileged rollback and arbitrary DNF repository mutations
- Drop unused legacy RPM migration metadata; krisCC has a single package identity

* Wed Sep 16 2026 krism-eu - 0.4.0-9
- Preserve --background startup mode after the krisCC technical rename
- Keep KrisOS session autostart from opening the main window

* Wed Sep 16 2026 krism-eu - 0.4.0-8
- Rename the package and executable to krisCC to avoid Fedora kcc collisions
- Rename QML, Polkit, desktop, icon and AppStream identities to krisCC/org.kriscc
- Keep only the original k-controlc transition Provides/Obsoletes

* Wed Sep 16 2026 krism-eu - 0.4.0-7
- Rename the RPM identity and executable to kcc
- Add Provides/Obsoletes for safe replacement of installed k-controlc packages
- Point package metadata at the renamed KCC repository

* Wed Sep 16 2026 krism-eu - 0.4.0-6
- Prefer current KrisOS package-state paths with a read-only legacy fallback
- Complete the KCC user-facing metadata rename without changing installed technical IDs
- Document safe base-image integration before the KrisOS owned-package snapshot

* Wed Sep 16 2026 krism-eu - 0.4.0-5
- Add a simple Podman container management page with size, status and common actions
- Begin the safe user-facing rename from K-ControlC to KCC while retaining package compatibility
- Keep RPM size and transaction details visible in software search

* Wed Sep 16 2026 krism-eu - 0.4.0-4
- Replace raw Flatpak command output with structured application cards
