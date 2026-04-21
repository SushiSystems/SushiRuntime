#!/bin/bash

# Script dizinine gec
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
cd "$SCRIPT_DIR"

# Config dosyasini oku
if [ -f "config.cfg" ]; then
    while IFS='=' read -r key value; do
        # Bos satirlari ve yorumlari atla
        [[ $key =~ ^#.* ]] || [[ -z $key ]] && continue
        # Degiskeni export et
        export "$key=$value"
    done < config.cfg
fi

# Intel oneAPI ortaminı yukle
if [ -f "$ONEAPI_ROOT/setvars.sh" ]; then
    source "$ONEAPI_ROOT/setvars.sh" --force > /dev/null 2>&1
fi

# Proje kok dizinine git
cd ../..

# CMake argümanlarını ayarla
CMAKE_ARGS="-B build -G Ninja -DCMAKE_BUILD_TYPE=Release"

if [ "$USE_VCPKG" = "true" ]; then
    echo "[INFO] Using vcpkg from $VCPKG_ROOT"
    CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
else
    echo "[INFO] Using system packages (apt/yum/etc.)"
fi

# Yapilandirma
if [ ! -f "build/build.ninja" ]; then
    echo "[INFO] Configuring CMake..."
    rm -rf build
    cmake $CMAKE_ARGS \
        -DCMAKE_CXX_COMPILER=icpx \
        -DCMAKE_C_COMPILER=icx \
        -DCMAKE_CXX_FLAGS="-fsycl"
fi

# Derle
echo "[INFO] Building..."
cmake --build build --config Release

if [ $? -eq 0 ]; then
    echo "[SUCCESS] Build completed successfully!"
else
    echo "[ERROR] Build failed."
fi
