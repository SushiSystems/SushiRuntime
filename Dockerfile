# Base: CUDA runtime (GTX 1060 = sm_61, Pascal)
FROM nvidia/cuda:12.4.1-devel-ubuntu22.04

# GCC 13 for full C++20 stdlib + base tools
RUN apt-get update && apt-get install -y software-properties-common && \
    add-apt-repository -y ppa:ubuntu-toolchain-r/test && \
    apt-get update && apt-get install -y \
    python3 build-essential cmake ninja-build git \
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

# intel/llvm nightly — pre-built SYCL toolchain with CUDA + OpenCL backends
ARG INTEL_LLVM_DATE=2026-06-12
RUN wget -q \
    https://github.com/intel/llvm/releases/download/nightly-${INTEL_LLVM_DATE}/sycl_linux.tar.gz \
    -O /tmp/sycl_linux.tar.gz && \
    mkdir -p /opt/intel-llvm && \
    tar -xzf /tmp/sycl_linux.tar.gz -C /opt/intel-llvm --strip-components=1 && \
    rm /tmp/sycl_linux.tar.gz

ENV PATH="/opt/intel-llvm/bin:$PATH"
ENV LD_LIBRARY_PATH="/opt/intel-llvm/lib:$LD_LIBRARY_PATH"

RUN echo "alias sushiruntime='/workspace/sushiruntime/.tools/sushiruntime.sh'" >> ~/.bashrc && \
    echo "alias sr='/workspace/sushiruntime/.tools/sushiruntime.sh'" >> ~/.bashrc

WORKDIR /workspace/sushiruntime
COPY . /workspace/sushiruntime/

RUN ln -sf /usr/lib/linux-tools/*/perf /usr/bin/perf 2>/dev/null || true

RUN find . -type f -name "*.sh" -exec sed -i 's/\r$//' {} + && \
    chmod +x .tools/*.sh

RUN mkdir -p build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release \
             -DCMAKE_CXX_COMPILER=clang++ \
             -DCMAKE_C_COMPILER=clang \
             -DSUSHI_ENABLE_CUDA=ON && \
    make VERBOSE=1 -j$(nproc)

CMD ["/bin/bash"]
