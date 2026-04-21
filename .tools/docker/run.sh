#!/bin/bash
echo "[INFO] Starting SushiRuntime developer container with volume..."
docker run -it \
    -v "$(pwd):/workspace/sushiruntime" \
    sushiruntime /bin/bash
