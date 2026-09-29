#!/usr/bin/env bash
set -euo pipefail

if [[ "$#" -ne 1 ]]; then
  echo "Usage: $0 /path/to/krisCC-X.Y.Z-1.fc45.x86_64.rpm" >&2
  exit 2
fi

rpm_path="$(realpath "$1")"
test -f "$rpm_path"
command -v podman >/dev/null

base_image="${KRISCC_F45_BASE_IMAGE:-quay.io/bootc-devel/fedora-bootc-45-minimal@sha256:9d010fe35ac8db7f0bcb8576b530ea443feed2d7c428a40a06c2f0c98aa86437}"
image_tag="${KRISCC_F45_SMOKE_TAG:-localhost/kriscc-f45-minimal-smoke:latest}"

if ! podman image exists "$base_image"; then
  podman pull "$base_image"
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
cp "$rpm_path" "$tmp/krisCC.rpm"

cat > "$tmp/Containerfile" <<'EOF'
ARG BASE_IMAGE
FROM ${BASE_IMAGE}

COPY krisCC.rpm /tmp/krisCC.rpm

RUN set -eux; \
    test "$(rpm -qp --qf '%{NAME}' /tmp/krisCC.rpm)" = krisCC; \
    test "$(rpm -qp --qf '%{RELEASE}' /tmp/krisCC.rpm)" = 1.fc45; \
    rpm -qa --qf '%{NAME}\t%{EPOCHNUM}:%{VERSION}-%{RELEASE}.%{ARCH}\n' \
      | LC_ALL=C sort -u > /tmp/base-nevra.before; \
    cut -f1 /tmp/base-nevra.before | LC_ALL=C sort -u > /tmp/base-names; \
    base_excludes="$(paste -sd, /tmp/base-names)"; \
    dnf5 -y \
      --setopt=updates-testing.enabled=false \
      --setopt=install_weak_deps=False \
      --setopt="excludepkgs=${base_excludes}" \
      install \
        qt6-qtbase \
        qt6-qtdeclarative \
        kf6-kirigami \
        polkit \
        rpm \
        dnf5 \
        dnf5-plugins \
        NetworkManager \
        bootc \
        tar \
        bash \
        coreutils \
        systemd; \
    : > /tmp/base-nevra.after; \
    while IFS= read -r pkg; do \
      rpm -q --qf '%{NAME}\t%{EPOCHNUM}:%{VERSION}-%{RELEASE}.%{ARCH}\n' "$pkg" \
        >> /tmp/base-nevra.after; \
    done < /tmp/base-names; \
    LC_ALL=C sort -u -o /tmp/base-nevra.after /tmp/base-nevra.after; \
    diff -u /tmp/base-nevra.before /tmp/base-nevra.after; \
    dnf5 check --dependencies; \
    if rpm -qa --qf '%{ARCH}\n' | grep -qx i686; then \
      echo 'ERROR: i686 package entered the Fedora 45 runtime layer' >&2; \
      exit 1; \
    fi; \
    rpm -Uvh --nosignature /tmp/krisCC.rpm; \
    test "$(rpm -q --qf '%{NAME} %{VERSION} %{RELEASE} %{ARCH}\n' krisCC)" = \
      "krisCC 0.7.10 1.fc45 x86_64"; \
    rpm -V krisCC; \
    for cmd in \
      /usr/bin/timeout \
      /usr/bin/systemctl \
      /usr/bin/loginctl \
      /usr/bin/resolvectl \
      /usr/bin/journalctl \
      /usr/bin/pkexec \
      /usr/bin/tar \
      /usr/bin/bash \
      /usr/bin/bootc \
      /usr/bin/dnf5 \
      /usr/bin/rpm; \
    do \
      test -x "$cmd"; \
    done; \
    dnf5 config-manager --help >/dev/null; \
    dnf5 repoquery --help >/dev/null; \
    sh -n /usr/libexec/kriscc/bootc-status; \
    grep -Fq -- '--format-version=1' /usr/libexec/kriscc/bootc-status; \
    test -z "$(ldd /usr/bin/krisCC | awk '/not found/{print}')"; \
    env \
      QT_QPA_PLATFORM=offscreen \
      QT_QUICK_BACKEND=software \
      QT_QUICK_CONTROLS_STYLE=Basic \
      KRISCC_SMOKE_TEST=1 \
      QT_FORCE_STDERR_LOGGING=1 \
      /usr/bin/krisCC --background; \
    rm -f /tmp/krisCC.rpm /tmp/base-nevra.before /tmp/base-nevra.after /tmp/base-names; \
    dnf5 clean all; \
    rm -f /var/log/dnf5.log; \
    bootc container lint
EOF

podman build \
  --pull=never \
  --build-arg "BASE_IMAGE=$base_image" \
  -t "$image_tag" \
  "$tmp"

podman run --rm --entrypoint /bin/bash "$image_tag" -lc '
  set -euxo pipefail
  rpm -V krisCC
  test "$(rpm -q --qf "%{VERSION}-%{RELEASE}.%{ARCH}" krisCC)" = "0.7.10-1.fc45.x86_64"
  test -z "$(ldd /usr/bin/krisCC | awk "/not found/{print}")"
  bootc container lint
'

printf 'Fedora 45 Minimal krisCC smoke image: %s\n' "$image_tag"
