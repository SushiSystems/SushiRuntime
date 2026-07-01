# Base: CUDA runtime (GTX 1060 = sm_61, Pascal)
FROM nvidia/cuda:12.4.1-devel-ubuntu22.04

# GCC 13 for a recent C++17 stdlib + base tools (any GCC >= 9 suffices; 13 is
# kept as a known-good, well-tested libstdc++ for the clang -fsycl host pass)
RUN apt-get update && apt-get install -y software-properties-common && \
    add-apt-repository -y ppa:ubuntu-toolchain-r/test && \
    apt-get update && apt-get install -y \
    python3 python3-pip build-essential ninja-build git \
    pkg-config libhwloc-dev libgtest-dev \
    dos2unix linux-tools-generic wget \
    gcc-13 g++-13 ocl-icd-opencl-dev ocl-icd-libopencl1 \
    && rm -rf /var/lib/apt/lists/*

# CMake 3.31 — Ubuntu 22.04 apt ships 3.22 which lacks icpx CXX-standard flag mapping
RUN apt-get update && apt-get install -y curl ca-certificates gpg && \
    curl -fsSL https://apt.kitware.com/keys/kitware-archive-latest.asc \
        | gpg --dearmor -o /usr/share/keyrings/kitware-archive-keyring.gpg && \
    echo 'deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ jammy main' \
        > /etc/apt/sources.list.d/kitware.list && \
    apt-get update && apt-get install -y cmake && \
    rm -rf /var/lib/apt/lists/*

# Intel OpenCL CPU runtime — exposes Ryzen 7600X as a SYCL device
RUN apt-get update && apt-get install -y curl ca-certificates && \
    curl -fsSL https://apt.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB \
        | gpg --dearmor > /usr/share/keyrings/oneapi-archive-keyring.gpg && \
    echo "deb [signed-by=/usr/share/keyrings/oneapi-archive-keyring.gpg] https://apt.repos.intel.com/oneapi all main" \
        > /etc/apt/sources.list.d/oneAPI.list && \
    apt-get update && apt-get install -y intel-oneapi-runtime-opencl && \
    rm -rf /var/lib/apt/lists/*

# intel/llvm nightly SYCL bundle — a pre-built, vendor-neutral clang++ -fsycl
# toolchain with CUDA + OpenCL backends. This is the `intel-llvm` toolchain the
# image builds with — the project's primary, tested path.
ARG INTEL_LLVM_DATE=2026-06-12
RUN wget -q \
    https://github.com/intel/llvm/releases/download/nightly-${INTEL_LLVM_DATE}/sycl_linux.tar.gz \
    -O /tmp/sycl_linux.tar.gz && \
    mkdir -p /opt/llvm-sycl && \
    tar -xzf /tmp/sycl_linux.tar.gz -C /opt/llvm-sycl --strip-components=1 && \
    rm /tmp/sycl_linux.tar.gz

ENV PATH="/opt/llvm-sycl/bin:$PATH"
ENV LD_LIBRARY_PATH="/opt/llvm-sycl/lib:$LD_LIBRARY_PATH"

# AdaptiveCpp (acpp) — the project's secondary, fully vendor-neutral SYCL toolchain,
# built from source against stock open-source LLVM (not the intel/llvm fork). This
# is the path with zero vendor compiler dependency. Built against the distro's
# clang/llvm so it stays pure open source; the CUDA backend uses the base image's
# CUDA toolkit. Override the version with --build-arg ADAPTIVECPP_VERSION=...
ARG ADAPTIVECPP_VERSION=v24.10.0
# clang-17 from apt.llvm.org. The project compiles as C++17, so the old C++23
# <expected> constraint no longer applies; clang-17 is retained as a known-good
# host clang for the AdaptiveCpp build against libstdc++ 13.
ARG ACPP_LLVM=17
RUN apt-get update && apt-get install -y --no-install-recommends gnupg lsb-release && \
    wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key \
        | gpg --dearmor -o /usr/share/keyrings/llvm.gpg && \
    echo "deb [signed-by=/usr/share/keyrings/llvm.gpg] http://apt.llvm.org/jammy/ llvm-toolchain-jammy-${ACPP_LLVM} main" \
        > /etc/apt/sources.list.d/llvm.list && \
    apt-get update && apt-get install -y --no-install-recommends \
        libboost-context-dev libboost-fiber-dev \
        clang-${ACPP_LLVM} llvm-${ACPP_LLVM}-dev libclang-${ACPP_LLVM}-dev \
        lld-${ACPP_LLVM} && \
    git clone --depth 1 --branch ${ADAPTIVECPP_VERSION} \
        https://github.com/AdaptiveCpp/AdaptiveCpp.git /tmp/acpp && \
    cmake -S /tmp/acpp -B /tmp/acpp/build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/opt/adaptivecpp \
        -DLLVM_DIR=/usr/lib/llvm-${ACPP_LLVM}/lib/cmake/llvm \
        -DCLANG_INSTALL_PREFIX=/usr/lib/llvm-${ACPP_LLVM} \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 && \
    cmake --build /tmp/acpp/build --target install && \
    rm -rf /tmp/acpp /var/lib/apt/lists/*
ENV PATH="/opt/adaptivecpp/bin:$PATH"

# Optional Intel oneAPI DPC++ compiler (icpx/icx) — the `supported` toolchain,
# kept for VTune/Advisor profiling. OFF by default to keep the image lean: icpx
# adds several GB. Enable with `docker build --build-arg WITH_ONEAPI=1 ...`
# (or `sr docker build --oneapi`). The oneAPI apt repo + keyring are already
# configured above for the OpenCL CPU runtime, so this only adds the compiler
# package. It installs under /opt/intel/oneapi; the `sr` CLI sources its
# setvars.sh automatically (cli/config.toml [tool.linux] oneapi_root) so
# `sr toolchain oneapi && sr build` works with no extra steps.
ARG WITH_ONEAPI=0
RUN if [ "$WITH_ONEAPI" = "1" ]; then \
        apt-get update && \
        apt-get install -y --no-install-recommends intel-oneapi-compiler-dpcpp-cpp && \
        rm -rf /var/lib/apt/lists/* ; \
    else \
        echo "oneAPI compiler not installed (build with --build-arg WITH_ONEAPI=1 to add icpx)." ; \
    fi
# NOTE: oneAPI's setvars.sh is deliberately NOT sourced from ~/.bashrc. Doing so
# unconditionally prepends oneAPI's lib dir to LD_LIBRARY_PATH in every shell, so
# even an llvm/adaptivecpp binary would load oneAPI's libsycl/libur_loader while
# its CUDA backend resolves the llvm-sycl libur_adapter_cuda — a Unified Runtime
# mismatch that segfaults in SYCL device discovery (urVirtualMemMap). The `sr`
# CLI sources setvars only for the oneapi toolchain (cli/sushiruntime/env.py).

# Keep GPU kernel launches ASYNC. With CUDA_LAUNCH_BLOCKING=1 every launch runs
# synchronously, which makes our async/overlap tests fail (submit looks blocking).
# It bit us once, so we pin it off in the image. Override at `docker run` only if
# you are deliberately debugging a kernel.
ENV CUDA_LAUNCH_BLOCKING=0

WORKDIR /workspace/sushiruntime
COPY . /workspace/sushiruntime/

RUN ln -sf /usr/lib/linux-tools/*/perf /usr/bin/perf 2>/dev/null || true

# Normalize line endings on any shell helpers carried in from a Windows checkout.
RUN find . -type f -name "*.sh" -exec sed -i 's/\r$//' {} + 2>/dev/null || true

# Install the `sr` CLI system-wide (the container is already isolated, so no venv
# is needed). Pin /usr/bin/python3 explicitly: the intel/llvm bundle prepends its
# own bin to PATH, and python3-pip belongs to the system python — installing and
# running under the same interpreter avoids a split. Verify the import right after
# so a bad build context fails loudly here instead of confusingly downstream.
RUN /usr/bin/python3 -m pip install --no-cache-dir --upgrade pip setuptools wheel && \
    /usr/bin/python3 -m pip install --no-cache-dir /workspace/sushiruntime/cli && \
    /usr/bin/python3 -c "import sushiruntime.cli; print('sr CLI installed OK')" && \
    printf "alias sr='/usr/bin/python3 -m sushiruntime'\nalias sushiruntime='/usr/bin/python3 -m sushiruntime'\n" >> ~/.bashrc

# The image is a ready build/run *environment*, not a pre-built artifact: both
# toolchains (intel-llvm primary, AdaptiveCpp secondary) plus the deps and the
# `sr` CLI are installed. Build on demand inside the container — there is no point
# baking a build into the image:
#   sr toolchain intel-llvm   # or: adaptivecpp
#   sr build
# The default toolchain is intel-llvm; switch with `sr toolchain`. The oneAPI
# toolchain (icpx) is available only when the image was built with WITH_ONEAPI=1
# (`sr docker build --oneapi`); then `sr toolchain oneapi && sr build`
# works (the CLI sources /opt/intel/oneapi/setvars.sh on its own).

CMD ["/bin/bash"]
