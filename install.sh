#!/usr/bin/env bash
# SushiRuntime one-script installer (Linux / Debian-Ubuntu).
#
# Bootstraps just enough to run the Python installer pipeline, then hands off to
# `sr setup all`, which installs every remaining dependency from
# cli/dependencies.toml, writes config.local.toml, and builds + smoke-tests.
#
# Usage (inside a checkout):
#   bash install.sh [--gpu] [--dry-run]
#
# Usage (bare machine, no checkout yet):
#   curl -fsSL https://raw.githubusercontent.com/sushisystems/sushiruntime/main/install.sh | bash -s -- --gpu
set -euo pipefail

REPO_URL="${SR_REPO_URL:-https://github.com/sushisystems/sushiruntime.git}"
GPU_FLAG=""
DRY_FLAG=""
for arg in "$@"; do
  case "$arg" in
    --gpu)     GPU_FLAG="--gpu" ;;
    --dry-run) DRY_FLAG="--dry-run" ;;
    *) echo "[WARN] unknown argument: $arg" ;;
  esac
done

log()  { printf '\033[1;34m[INFO]\033[0m %s\n' "$*"; }
err()  { printf '\033[1;31m[ERROR]\033[0m %s\n' "$*" >&2; }

SUDO=""
if [ "$(id -u)" -ne 0 ]; then SUDO="sudo"; fi

# 1. apt is required (Debian/Ubuntu only, per design).
if ! command -v apt-get >/dev/null 2>&1; then
  err "apt-get not found. Only Debian/Ubuntu are supported by this installer."
  exit 1
fi

# 2. Bootstrap python3 + pip + git if missing.
need_bootstrap=0
for tool in python3 git; do
  command -v "$tool" >/dev/null 2>&1 || need_bootstrap=1
done
python3 -m pip --version >/dev/null 2>&1 || need_bootstrap=1
if [ "$need_bootstrap" -eq 1 ]; then
  log "Installing bootstrap tools (python3, pip, git)..."
  $SUDO apt-get update
  $SUDO apt-get install -y python3 python3-pip git
fi

# 3. Locate the repo: a checkout containing this script, else clone it.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" 2>/dev/null && pwd || true)"
if [ -n "$SCRIPT_DIR" ] && [ -f "$SCRIPT_DIR/CMakeLists.txt" ]; then
  REPO_DIR="$SCRIPT_DIR"
else
  REPO_DIR="${SR_REPO_DIR:-$HOME/sushiruntime}"
  if [ ! -d "$REPO_DIR/.git" ]; then
    log "Cloning $REPO_URL -> $REPO_DIR"
    git clone "$REPO_URL" "$REPO_DIR"
  fi
fi
cd "$REPO_DIR"
log "Repo: $REPO_DIR"

# 4. Install the `sr` CLI into the current interpreter.
log "Installing the sr CLI..."
python3 -m pip install --upgrade pip >/dev/null
python3 -m pip install ./cli

# 5. Run the installer pipeline.
log "Running: sr setup all $GPU_FLAG $DRY_FLAG"
python3 -m sushiruntime setup all $GPU_FLAG $DRY_FLAG

log "Done. Try: sr project test --suite functional"
