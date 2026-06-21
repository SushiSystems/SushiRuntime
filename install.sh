#!/usr/bin/env bash
# SushiRuntime one-script installer (Linux).
#
# Bootstraps Python, pip, Git, CMake, and Ninja, then hands off to
# `sr setup all`. Supports Debian/Ubuntu (apt), Fedora/RHEL (dnf/yum),
# Arch (pacman), and openSUSE (zypper).
#
# Installs the lean intel-llvm + AdaptiveCpp SYCL toolchains by default, mirroring
# the Dockerfile. GPU/CUDA deps are included by default; Intel oneAPI (heavy) is
# opt-in.
#
# Usage (inside a checkout):
#   bash install.sh [--no-gpu] [--oneapi] [--dry-run]
#
# Usage (bare machine, no checkout yet):
#   curl -fsSL https://raw.githubusercontent.com/sushisystems/sushiruntime/main/install.sh | bash -s -- --no-gpu
set -euo pipefail

REPO_URL="${SR_REPO_URL:-https://github.com/sushisystems/sushiruntime.git}"
# GPU is on by default ("stock"); pass --no-gpu to skip CUDA deps.
GPU_FLAG="--gpu"
ONEAPI_FLAG=""
DRY_FLAG=""
for arg in "$@"; do
  case "$arg" in
    --gpu)     GPU_FLAG="--gpu" ;;
    --no-gpu)  GPU_FLAG="--no-gpu" ;;
    --oneapi)  ONEAPI_FLAG="--oneapi" ;;
    --dry-run) DRY_FLAG="--dry-run" ;;
    *) printf '[WARN] unknown argument: %s\n' "$arg" ;;
  esac
done

log() { printf '\033[1;34m[INFO]\033[0m %s\n' "$*"; }
err() { printf '\033[1;31m[ERROR]\033[0m %s\n' "$*" >&2; exit 1; }

SUDO=""
if [ "$(id -u)" -ne 0 ]; then SUDO="sudo"; fi

bootstrap_apt() {
    log "Installing bootstrap tools via apt..."
    $SUDO apt-get update -qq
    $SUDO apt-get install -y python3 python3-pip git cmake ninja-build
}

bootstrap_dnf() {
    log "Installing bootstrap tools via dnf..."
    $SUDO dnf install -y python3 python3-pip git cmake ninja-build
}

bootstrap_yum() {
    log "Installing bootstrap tools via yum..."
    $SUDO yum install -y python3 python3-pip git cmake ninja-build
}

bootstrap_pacman() {
    log "Installing bootstrap tools via pacman..."
    $SUDO pacman -Sy --noconfirm python python-pip git cmake ninja
}

bootstrap_zypper() {
    log "Installing bootstrap tools via zypper..."
    $SUDO zypper install -y python3 python3-pip git cmake ninja
}

need_bootstrap=0
for tool in python3 git cmake ninja; do
  command -v "$tool" >/dev/null 2>&1 || need_bootstrap=1
done
python3 -m pip --version >/dev/null 2>&1 || need_bootstrap=1

if [ "$need_bootstrap" -eq 1 ]; then
  if   command -v apt-get  >/dev/null 2>&1; then bootstrap_apt
  elif command -v dnf      >/dev/null 2>&1; then bootstrap_dnf
  elif command -v yum      >/dev/null 2>&1; then bootstrap_yum
  elif command -v pacman   >/dev/null 2>&1; then bootstrap_pacman
  elif command -v zypper   >/dev/null 2>&1; then bootstrap_zypper
  else
    err "No supported package manager found (apt, dnf, yum, pacman, zypper). Install python3, pip, git, cmake, and ninja manually, then re-run."
  fi
fi

# Locate or clone the repo.
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

# Install the sr CLI.
log "Installing the sr CLI..."
python3 cli/install.py

# Run the installer pipeline.
log "Running: sr setup all $GPU_FLAG $ONEAPI_FLAG $DRY_FLAG"
PIPX_BIN_DIR=$(python3 -m pipx environment --value PIPX_BIN_DIR)
SR_CMD="$PIPX_BIN_DIR/sr"
if [ ! -x "$SR_CMD" ]; then SR_CMD="sr"; fi
$SR_CMD setup all $GPU_FLAG $ONEAPI_FLAG $DRY_FLAG
SR_EXIT=$?
if [ "$SR_EXIT" -eq 0 ]; then
  log "Done. Try: sr project test --suite functional"
else
  exit "$SR_EXIT"
fi
