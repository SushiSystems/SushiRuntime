#!/bin/bash
set -e

echo "[1/4] Adding Intel GPG key..."
wget -qO /tmp/intel-gpg.pub https://apt.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB
gpg --dearmor < /tmp/intel-gpg.pub > /tmp/oneapi-archive-keyring.gpg
sudo cp /tmp/oneapi-archive-keyring.gpg /usr/share/keyrings/oneapi-archive-keyring.gpg
rm /tmp/intel-gpg.pub /tmp/oneapi-archive-keyring.gpg

echo "[2/4] Adding Intel oneAPI repo..."
echo "deb [signed-by=/usr/share/keyrings/oneapi-archive-keyring.gpg] https://apt.repos.intel.com/oneapi all main" | sudo tee /etc/apt/sources.list.d/oneAPI.list

echo "[3/4] Installing Intel OpenCL CPU runtime..."
sudo apt-get update
sudo apt-get install -y intel-oneapi-runtime-opencl

echo "[4/4] Done! Verify with: sycl-ls"
