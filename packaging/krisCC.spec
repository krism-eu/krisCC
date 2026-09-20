Name:           krisCC
Version:        0.7.0
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
Requires:       bootc
Requires:       tar

%description
krisCC is a compact personal Kirigami control center for KrisOS and Fedora bootc
systems. It focuses on persistent software management, Flatpak applications,
Podman containers, bootc updates, practical maintenance tools, diagnostics,
recovery and local configuration/home backups without duplicating Plasma System
Settings.

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
%{_libexecdir}/kriscc/bootc-status
%{_libexecdir}/kriscc/maintenance
%{_datadir}/applications/krisCC.desktop
%{_datadir}/polkit-1/actions/org.kriscc.controlcenter.policy
%{_datadir}/metainfo/org.kriscc.KrisCC.metainfo.xml
%{_datadir}/icons/hicolor/scalable/apps/krisCC.svg

%changelog
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
- Add direct Flatpak install/remove actions and clearer remote views
- Make RPM sizes, dependency preview and transaction totals explicit

* Wed Sep 16 2026 krism-eu - 0.4.0-3
- Add top navigation tabs, dedicated Flatpak and command-bookmark pages
- Add RPM transaction previews, package origin filters and repository-file addition
- Clarify external tool availability, active services and the unified backup workflow

* Wed Sep 16 2026 krism-eu - 0.4.0-2
- Fix DNF5 search metadata parsing
- Refine software and repository presentation

* Wed Sep 16 2026 krism-eu - 0.4.0-1
- Move the UI to Kirigami and remove Plasma configuration duplication
- Add package inventory, upgrades, recent packages and repository management
- Add personal maintenance tools and local config/home backup snapshots

* Tue Sep 15 2026 krism-eu - 0.3.0-1
- Initial standalone K-ControlC package
