#!/bin/bash
set -e

echo "[1/4] Adding NVIDIA container toolkit repo..."
tee /etc/apt/sources.list.d/nvidia-container-toolkit.list > /dev/null <<EOF
deb [signed-by=/usr/share/keyrings/nvidia-container-toolkit-keyring.gpg] https://nvidia.github.io/libnvidia-container/stable/deb/amd64 /
EOF

echo "[2/4] Installing nvidia-container-toolkit..."
apt-get update && apt-get install -y nvidia-container-toolkit

echo "[3/4] Configuring Docker runtime..."
nvidia-ctk runtime configure --runtime=docker

echo "[4/4] Restarting Docker..."
service docker restart

echo "Done! Run: docker info | grep -i runtime"
