# Base: CUDA runtime (GTX 1060 = sm_61, Pascal)
FROM nvidia/cuda:12.4.1-devel-ubuntu22.04

# GCC 13 for full C++23 stdlib + base tools
RUN apt-get update && apt-get install -y software-properties-common && \
    add-apt-repository -y ppa:ubuntu-toolchain-r/test && \
    apt-get update && apt-get install -y \
    python3 python3-pip build-essential cmake ninja-build git \
    pkg-config libhwloc-dev libgtest-dev \
    dos2unix linux-tools-generic wget \
    gcc-13 g++-13 ocl-icd-opencl-dev ocl-icd-libopencl1 \
    && rm -rf /var/lib/apt/lists/*

# Intel OpenCL CPU runtime — exposes Ryzen 7600X as a SYCL device
RUN apt-get update && apt-get install -y curl ca-certificates && \
    curl -fsSL https://apt.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB \
        | gpg --dearmor > /usr/share/keyrings/oneapi-archive-keyring.gpg && \
    echo "deb [signed-by=/usr/share/keyrings/oneapi-archive-keyring.gpg] https://apt.repos.intel.com/oneapi all main" \
        > /etc/apt/sources.list.d/oneAPI.list && \
    apt-get update && apt-get install -y intel-oneapi-runtime-opencl && \
    rm -rf /var/lib/apt/lists/*

# Clang/LLVM SYCL bundle (intel/llvm nightly build) — a pre-built, vendor-neutral
# clang++ -fsycl toolchain with CUDA + OpenCL backends. This is the `llvm`
# toolchain the image builds with. AdaptiveCpp is the project's primary host
# toolchain; the container pins `llvm` because this bundle is the tested CI path.
ARG INTEL_LLVM_DATE=2026-06-12
RUN wget -q \
    https://github.com/intel/llvm/releases/download/nightly-${INTEL_LLVM_DATE}/sycl_linux.tar.gz \
    -O /tmp/sycl_linux.tar.gz && \
    mkdir -p /opt/llvm-sycl && \
    tar -xzf /tmp/sycl_linux.tar.gz -C /opt/llvm-sycl --strip-components=1 && \
    rm /tmp/sycl_linux.tar.gz

ENV PATH="/opt/llvm-sycl/bin:$PATH"
ENV LD_LIBRARY_PATH="/opt/llvm-sycl/lib:$LD_LIBRARY_PATH"

# AdaptiveCpp (acpp) — the project's primary, fully vendor-neutral SYCL toolchain,
# built from source against stock open-source LLVM (not the intel/llvm fork). This
# is the path with zero vendor compiler dependency. Built against the distro's
# clang/llvm so it stays pure open source; the CUDA backend uses the base image's
# CUDA toolkit. Override the version with --build-arg ADAPTIVECPP_VERSION=...
ARG ADAPTIVECPP_VERSION=v24.10.0
ARG ACPP_LLVM=16
RUN apt-get update && apt-get install -y --no-install-recommends \
        libboost-context-dev libboost-fiber-dev \
        clang-${ACPP_LLVM} llvm-${ACPP_LLVM}-dev libclang-${ACPP_LLVM}-dev \
        lld-${ACPP_LLVM} && \
    git clone --depth 1 --branch ${ADAPTIVECPP_VERSION} \
        https://github.com/AdaptiveCpp/AdaptiveCpp.git /tmp/acpp && \
    cmake -S /tmp/acpp -B /tmp/acpp/build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/opt/adaptivecpp \
        -DLLVM_DIR=/usr/lib/llvm-${ACPP_LLVM}/lib/cmake/llvm \
        -DCLANG_INSTALL_PREFIX=/usr/lib/llvm-${ACPP_LLVM} && \
    cmake --build /tmp/acpp/build --target install && \
    rm -rf /tmp/acpp /var/lib/apt/lists/*
ENV PATH="/opt/adaptivecpp/bin:$PATH"

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

# Both toolchains are installed: AdaptiveCpp (acpp, primary/vendor-neutral) and
# the Clang/LLVM bundle (llvm). The image's validation build below pins `llvm`
# as the currently tested path; switch any time with `sr select adaptivecpp`.
RUN /usr/bin/python3 -m sushiruntime select llvm

# Build through the CLI (exercises `sr` end-to-end). The `llvm` toolchain uses
# the bundled clang++ -fsycl, and CUDA defaults ON on Linux.
#
# Two pre-builds so the image validates BOTH compile configurations and ships all
# test/benchmark binaries:
#   1. standalone  — the offline-first default path (SR_DISTRIBUTED off): the
#                    functional suite + the non-distributed benchmarks.
#   2. distributed — the master-worker layer (SR_DISTRIBUTED on): adds the
#                    distributed unit/e2e tests and the offload benchmark. This
#                    reconfigures build/ in place (stamp .sushi_dist), so build/
#                    ends in distributed state — a superset of binaries. The
#                    runtime role still defaults to Standalone; the build flag
#                    only governs which code is compiled in, not how it runs.
RUN /usr/bin/python3 -m sushiruntime project build --type release && \
    /usr/bin/python3 -m sushiruntime project build --type release --distributed

CMD ["/bin/bash"]
