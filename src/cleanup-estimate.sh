#!/usr/bin/bash
set -u

DU_BIN="${KRISCC_DU_BIN:-/usr/bin/du}"
JOURNALCTL_BIN="${KRISCC_JOURNALCTL_BIN:-/usr/bin/journalctl}"
TRASH_PATH="${KRISCC_TRASH_PATH:-$HOME/.local/share/Trash}"
DNF_CACHE_PATH="${KRISCC_DNF_CACHE_PATH:-/var/cache/libdnf5}"

measure_directory() {
    local label="$1"
    local path="$2"
    local out size

    if [[ ! -e "$path" ]]; then
        printf '%s: assente\n' "$label"
        return 0
    fi
    if [[ ! -r "$path" || ! -x "$path" ]]; then
        printf '%s: accesso negato\n' "$label"
        return 0
    fi
    if out="$($DU_BIN -sh -- "$path" 2>/dev/null)"; then
        size="${out%%$'\t'*}"
        if [[ "$size" == "$out" ]]; then
            size="${out%% *}"
        fi
        if [[ -n "$size" ]]; then
            printf '%s: %s\n' "$label" "$size"
        else
            printf '%s: non misurabile\n' "$label"
        fi
    else
        printf '%s: non misurabile\n' "$label"
    fi
}

measure_journal() {
    local out size
    if [[ ! -x "$JOURNALCTL_BIN" ]]; then
        printf 'Journal: comando non disponibile\n'
        return 0
    fi
    if out="$(LC_ALL=C "$JOURNALCTL_BIN" --disk-usage 2>/dev/null)"; then
        size="${out#*take up }"
        if [[ "$size" != "$out" ]]; then
            size="${size%% in the file system.*}"
        else
            size="${out#*use }"
            size="${size%%.*}"
        fi
        if [[ -n "$size" && "$size" != "$out" ]]; then
            printf 'Journal: circa %s\n' "$size"
        elif [[ -n "$out" ]]; then
            printf 'Journal: %s\n' "$out"
        else
            printf 'Journal: non misurabile\n'
        fi
    else
        printf 'Journal: non misurabile\n'
    fi
}

measure_directory 'Cestini' "$TRASH_PATH"
measure_journal
measure_directory 'Cache DNF' "$DNF_CACHE_PATH"
printf 'Flatpak inutilizzati: calcolo esatto durante la pulizia\n'
