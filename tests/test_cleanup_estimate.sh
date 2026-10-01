#!/usr/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HELPER="$ROOT/src/cleanup-estimate.sh"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

mkdir -p "$TMP/bin" "$TMP/home/.local/share/Trash" "$TMP/dnf"

cat > "$TMP/bin/du-ok" <<'EOF'
#!/usr/bin/bash
printf '0\t%s\n' "${@: -1}"
EOF
chmod +x "$TMP/bin/du-ok"

cat > "$TMP/bin/du-fail" <<'EOF'
#!/usr/bin/bash
printf 'partial-output' >&2
exit 1
EOF
chmod +x "$TMP/bin/du-fail"

cat > "$TMP/bin/journal-ok" <<'EOF'
#!/usr/bin/bash
printf 'Archived and active journals take up 0B in the file system.\n'
EOF
chmod +x "$TMP/bin/journal-ok"

cat > "$TMP/bin/journal-fail" <<'EOF'
#!/usr/bin/bash
printf 'partial-journal' >&2
exit 1
EOF
chmod +x "$TMP/bin/journal-fail"

run_helper() {
    HOME="$TMP/home" \
    KRISCC_TRASH_PATH="${KRISCC_TRASH_PATH:-$TMP/home/.local/share/Trash}" \
    KRISCC_DNF_CACHE_PATH="${KRISCC_DNF_CACHE_PATH:-$TMP/dnf}" \
    KRISCC_DU_BIN="${KRISCC_DU_BIN:-$TMP/bin/du-ok}" \
    KRISCC_JOURNALCTL_BIN="${KRISCC_JOURNALCTL_BIN:-$TMP/bin/journal-ok}" \
    /usr/bin/bash "$HELPER"
}

out="$(run_helper)"
grep -Fxq 'Cestini: 0' <<<"$out"
grep -Fxq 'Journal: circa 0B' <<<"$out"
grep -Fxq 'Cache DNF: 0' <<<"$out"
grep -Fxq 'Flatpak inutilizzati: calcolo esatto durante la pulizia' <<<"$out"
test "$(wc -l <<<"$out")" -eq 4

rm -rf "$TMP/home/.local/share/Trash"
out="$(run_helper)"
grep -Fxq 'Cestini: assente' <<<"$out"
grep -Fxq 'Journal: circa 0B' <<<"$out"

mkdir -p "$TMP/home/.local/share/Trash"
chmod 000 "$TMP/home/.local/share/Trash"
# Root can bypass mode bits; use a failing producer to exercise the explicit read-error path portably.
out="$(KRISCC_DU_BIN="$TMP/bin/du-fail" run_helper)"
grep -Fxq 'Cestini: non misurabile' <<<"$out"
grep -Fxq 'Cache DNF: non misurabile' <<<"$out"
grep -Fxq 'Journal: circa 0B' <<<"$out"
chmod 700 "$TMP/home/.local/share/Trash"

out="$(KRISCC_JOURNALCTL_BIN="$TMP/bin/journal-fail" run_helper)"
grep -Fxq 'Journal: non misurabile' <<<"$out"
grep -Fxq 'Cache DNF: 0' <<<"$out"
test "$(wc -l <<<"$out")" -eq 4
