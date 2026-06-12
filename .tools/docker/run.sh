#!/bin/bash
# SushiRuntime Docker Run Script
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$( cd "$DIR/../.." && pwd )"

echo "[INFO] Starting SushiRuntime developer container with volume..."

ADMIN_FLAGS=""
if [[ "$*" == *"--admin"* ]]; then
    echo "[INFO] Running in ADMIN mode (privileged)"
    ADMIN_FLAGS="--privileged --cap-add=SYS_ADMIN"
fi

docker run -it --rm \
    --gpus all \
    $ADMIN_FLAGS \
    -v "$PROJECT_ROOT:/workspace/sushiruntime" \
    sushiruntime /bin/bash
