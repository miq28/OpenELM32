#!/bin/sh
set -eu

stage=0
if [ "${1:-}" = "--stage" ] || [ "${1:-}" = "-Stage" ]; then
    stage=1
fi

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source_path="$repo_root/src/private_identity_overrides.h"
archive_name="private_identity_overrides.h.7z"
archive_path="$repo_root/$archive_name"

if [ ! -f "$source_path" ]; then
    echo "[private-identity] No src/private_identity_overrides.h file found; skipping encrypted backup."
    exit 0
fi

if [ -f "$archive_path" ] && [ "$archive_path" -nt "$source_path" ]; then
    echo "[private-identity] Encrypted backup is up to date; skipping."
    exit 0
fi

seven_zip=""
if command -v 7z >/dev/null 2>&1; then
    seven_zip=$(command -v 7z)
elif command -v 7zz >/dev/null 2>&1; then
    seven_zip=$(command -v 7zz)
else
    echo "7z/7zz was not found on PATH. Install p7zip/7zip before committing." >&2
    exit 1
fi

password=${OPENELM_PRIVATE_BACKUP_PASSWORD:-}
if [ -z "$password" ]; then
    if [ -t 0 ]; then
        printf "Password for private_identity_overrides.h.7z: " >&2
        stty -echo
        read password
        stty echo
        printf "\n" >&2
    else
        echo "Set OPENELM_PRIVATE_BACKUP_PASSWORD or run from an interactive terminal." >&2
        exit 1
    fi
fi

if [ -z "$password" ]; then
    echo "Encrypted backup password cannot be empty." >&2
    exit 1
fi

rm -f "$archive_path"

cd "$repo_root"
"$seven_zip" a -t7z "$archive_name" "src/private_identity_overrides.h" -mhe=on "-p$password"

if [ "$stage" -eq 1 ]; then
    git -C "$repo_root" add -- "$archive_name"
fi

echo "[private-identity] Updated encrypted backup: $archive_name"
