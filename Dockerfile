# Use an official Intel oneAPI base image which provides SYCL/DPC++
FROM intel/oneapi-basekit:latest

# Install essential build tools and dependencies (hwloc)
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    pkg-config \
    libhwloc-dev \
    && rm -rf /var/lib/apt/lists/*

# Set the working directory
WORKDIR /workspace/SushiRuntime

# Copy local repository into the container
COPY . /workspace/SushiRuntime/

# Generate build system and compile the project
RUN mkdir -p build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc)

# Default to bash for interactive development
CMD ["/bin/bash"]
