# Use an official Intel oneAPI base image which provides SYCL/DPC++
FROM intel/oneapi-basekit:latest

# Install essential build tools and dependencies
RUN apt-get update && apt-get install -y \
    python3 \
    build-essential \
    cmake \
    ninja-build \
    git \
    pkg-config \
    libhwloc-dev \
    libgtest-dev \
    && rm -rf /var/lib/apt/lists/*

# Add aliases for the manager script
RUN echo "alias sushiruntime='/workspace/sushiruntime/.tools/sushiruntime.sh'" >> ~/.bashrc && \
    echo "alias sr='/workspace/sushiruntime/.tools/sushiruntime.sh'" >> ~/.bashrc

# Set the working directory
WORKDIR /workspace/sushiruntime

# Copy local repository into the container
COPY . /workspace/sushiruntime/

# Generate build system and compile the project
RUN mkdir -p build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release \
             -DCMAKE_CXX_COMPILER=icpx \
             -DCMAKE_C_COMPILER=icx && \
    make -j$(nproc)

# Default to bash for interactive development
CMD ["/bin/bash"]
