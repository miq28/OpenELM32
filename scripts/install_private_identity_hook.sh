#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
hook_dir="$repo_root/.git/hooks"
hook_path="$hook_dir/pre-commit"

if [ ! -d "$hook_dir" ]; then
    echo "Git hooks directory not found: $hook_dir" >&2
    exit 1
fi

cat > "$hook_path" <<'EOF'
#!/bin/sh
sh "scripts/private_identity_backup.sh" --stage
EOF

chmod +x "$hook_path"
echo "[private-identity] Installed pre-commit hook: .git/hooks/pre-commit"
echo "[private-identity] Set OPENELM_PRIVATE_BACKUP_PASSWORD to avoid an interactive password prompt."
