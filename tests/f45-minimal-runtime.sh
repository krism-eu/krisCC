#!/usr/bin/env bash
set -euo pipefail

[[ $# -eq 1 ]] || {
    echo "Usage: $0 /path/to/krisCC-X.Y.Z-1.fc45.x86_64.rpm" >&2
    exit 2
}

RPM_PATH="$(realpath "$1")"
test -f "$RPM_PATH"
command -v podman >/dev/null

BASE_IMAGE="${KRISCC_F45_BASE_IMAGE:-quay.io/bootc-devel/fedora-bootc-45-minimal:latest}"
IMAGE_TAG="${KRISCC_F45_SMOKE_TAG:-localhost/kriscc-f45-minimal-smoke:latest}"

podman pull "$BASE_IMAGE"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

cp "$RPM_PATH" "$tmp/krisCC.rpm"

cat > "$tmp/Containerfile" <<'INNER'
ARG BASE_IMAGE
FROM ${BASE_IMAGE}

# Fedora 45 is still Branched. Synchronize the current compose with the
# repositories it advertises before testing the krisCC transaction itself.
RUN set -eux; \
    dnf5 -y \
      --setopt=install_weak_deps=False \
      --exclude='*.i686' \
      distro-sync; \
    dnf5 check --dependencies; \
    ! rpm -qa --qf '%{ARCH}\n' | grep -qx i686; \
    dnf5 clean all; \
    rm -rf /run/dnf /var/cache/libdnf5 /var/lib/dnf/repos; \
    rm -f /var/log/dnf5.log /var/cache/ldconfig/aux-cache

COPY krisCC.rpm /tmp/krisCC.rpm

RUN set -eux; \
    test "$(rpm -qp --qf '%{NAME}' /tmp/krisCC.rpm)" = krisCC; \
    test "$(rpm -qp --qf '%{VERSION}-%{RELEASE}.%{ARCH}' /tmp/krisCC.rpm)" = \
      '0.8.2-1.fc45.x86_64'; \
    for req in \
      /usr/bin/timeout \
      /usr/bin/systemctl \
      /usr/bin/loginctl \
      /usr/bin/resolvectl \
      /usr/bin/journalctl; \
    do \
      rpm -qp --requires /tmp/krisCC.rpm | grep -Fxq "$req"; \
    done; \
    ! rpm -qp --requires /tmp/krisCC.rpm | grep -Eq '^coreutils([[:space:]<=>]|$)'; \
    ! rpm -qp --requires /tmp/krisCC.rpm | grep -Eq '^systemd([[:space:]<=>]|$)'; \
    rpm -qa \
      --qf '%{NAME}\t%{EPOCHNUM}:%{VERSION}-%{RELEASE}.%{ARCH}\n' \
      | LC_ALL=C sort -u > /tmp/base.before; \
    dnf5 -y \
      --setopt=install_weak_deps=False \
      --setopt=localpkg_gpgcheck=False \
      --exclude='*.i686' \
      install /tmp/krisCC.rpm; \
    rpm -qa \
      --qf '%{NAME}\t%{EPOCHNUM}:%{VERSION}-%{RELEASE}.%{ARCH}\n' \
      | LC_ALL=C sort -u > /tmp/all.after; \
    comm -23 /tmp/base.before /tmp/all.after > /tmp/base.changed; \
    if test -s /tmp/base.changed; then \
      echo 'ERROR: krisCC changed packages belonging to the synchronized base:' >&2; \
      cat /tmp/base.changed >&2; \
      exit 1; \
    fi; \
    dnf5 check --dependencies; \
    ! rpm -qa --qf '%{ARCH}\n' | grep -qx i686; \
    rpm -V krisCC; \
    test -z "$(ldd /usr/bin/krisCC | awk '/not found/{print}')"; \
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
    env \
      QT_QPA_PLATFORM=offscreen \
      QT_QUICK_BACKEND=software \
      QT_QUICK_CONTROLS_STYLE=Basic \
      KRISCC_SMOKE_TEST=1 \
      QT_FORCE_STDERR_LOGGING=1 \
      /usr/bin/krisCC --background; \
    rm -f /tmp/krisCC.rpm /tmp/base.before /tmp/all.after /tmp/base.changed; \
    dnf5 clean all; \
    rm -rf /run/dnf /var/cache/libdnf5 /var/lib/dnf/repos; \
    rm -f /var/log/dnf5.log /var/cache/ldconfig/aux-cache
INNER

podman build \
  --pull=always \
  --build-arg "BASE_IMAGE=$BASE_IMAGE" \
  -t "$IMAGE_TAG" \
  "$tmp"

podman run --rm \
  --entrypoint /bin/bash \
  "$IMAGE_TAG" \
  -lc '
    set -euxo pipefail
    rpm -V krisCC
    test "$(rpm -q --qf "%{VERSION}-%{RELEASE}.%{ARCH}" krisCC)" = \
      "0.8.2-1.fc45.x86_64"
    test -z "$(ldd /usr/bin/krisCC | awk "/not found/{print}")"
  '

echo 'PASS: Fedora 45 Branched Minimal + krisCC'
