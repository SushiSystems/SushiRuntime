# gpu/DetectGpu.cmake — resolve which GPU backend to build.
#
# Single responsibility: turn the SR_GPU_BACKEND request into one concrete vendor
# name in SR_GPU_BACKEND_RESOLVED, and include that vendor's backend file. This is
# the single source of truth for GPU selection — the CLI no longer duplicates the
# nvcc/libdevice check in Python; it just passes SR_GPU_BACKEND=auto and reads the
# result here.
#
# Precedence (first that applies wins):
#   1. explicit SR_GPU_BACKEND != auto        (cuda|rocm|intel|cpu|none)
#   2. legacy SR_ENABLE_CUDA=ON               -> cuda   (back-compat)
#   3. auto-detect from the host toolchains    (nvcc / hipcc|rocminfo), else cpu

set(SR_GPU_BACKEND "auto" CACHE STRING
    "GPU compute backend: auto (detect), cuda, rocm, intel, cpu, or none")
set_property(CACHE SR_GPU_BACKEND PROPERTY STRINGS auto cuda rocm intel cpu none)

set(_sr_backend "${SR_GPU_BACKEND}")

# Back-compat: honor the old -DSR_ENABLE_CUDA=ON switch when no explicit backend
# was chosen. Defaulted OFF so it never forces CPU over an auto-detected GPU.
option(SR_ENABLE_CUDA "Legacy switch: force the SYCL CUDA (nvptx64) backend" OFF)
if(_sr_backend STREQUAL "auto" AND SR_ENABLE_CUDA)
    set(_sr_backend "cuda")
endif()

# Did a human name this backend, or did we guess it? A vendor file that finds its
# toolkit installed but no device consults this to choose between failing and
# declining (see the dispatch below). SR_ENABLE_CUDA counts as naming it -- it is
# the legacy spelling of the same request, so it must not be downgraded to a
# guess just because SR_GPU_BACKEND itself still reads 'auto'.
if(_sr_backend STREQUAL "auto")
    set(SR_GPU_BACKEND_EXPLICIT FALSE)
else()
    set(SR_GPU_BACKEND_EXPLICIT TRUE)
endif()

if(_sr_backend STREQUAL "auto")
    # The SYCL CUDA/nvptx and amdgcn passes need the vendor toolkit's device
    # libraries (libdevice / ROCm device libs), so a mere driver is not enough:
    # probe for the *compiler* toolchain, not the runtime.
    # The apt CUDA/ROCm toolkits do not add themselves to PATH, so also look in
    # their well-known install prefixes (/usr/local/cuda* for nvcc, /opt/rocm*
    # for hipcc) — otherwise auto-detect wrongly falls back to CPU on a machine
    # that actually has the toolkit installed.
    find_program(SR_NVCC nvcc
        PATHS /usr/local/cuda/bin /usr/local/cuda-*/bin)
    find_program(SR_HIPCC NAMES hipcc rocminfo
        PATHS /opt/rocm/bin /opt/rocm-*/bin)
    if(SR_NVCC)
        set(_sr_backend "cuda")
    elseif(SR_HIPCC)
        set(_sr_backend "rocm")
    else()
        # No accelerator toolkit — build the portable CPU/OpenCL (spir64) path.
        set(_sr_backend "cpu")
    endif()
    message(STATUS "GPU auto-detect: selected '${_sr_backend}' backend.")
endif()

set(SR_GPU_BACKEND_RESOLVED "${_sr_backend}" CACHE INTERNAL "Resolved GPU backend")

# Dispatch to the vendor strategy. Each file is self-contained and only appends to
# the GpuBackend accumulators; adding a vendor means adding a branch here + a file.
#
# A vendor file may also decline. Auto-detect selects a backend by finding its
# *toolkit*, which proves a compiler is installed and not that a device exists, so
# a backend can discover at include time that it has no hardware to target. Rather
# than resolve that itself, it sets SR_GPU_BACKEND_DECLINED and returns, and the
# demotion happens here -- this file stays the single writer of
# SR_GPU_BACKEND_RESOLVED, which the CLI reads back.
set(SR_GPU_BACKEND_DECLINED FALSE)

if(_sr_backend STREQUAL "cuda")
    include("${CMAKE_CURRENT_LIST_DIR}/Cuda.cmake")
elseif(_sr_backend STREQUAL "rocm")
    include("${CMAKE_CURRENT_LIST_DIR}/Rocm.cmake")
elseif(_sr_backend STREQUAL "intel")
    include("${CMAKE_CURRENT_LIST_DIR}/IntelGpu.cmake")
elseif(_sr_backend STREQUAL "cpu" OR _sr_backend STREQUAL "none")
    include("${CMAKE_CURRENT_LIST_DIR}/CpuFallback.cmake")
else()
    message(FATAL_ERROR "Unknown SR_GPU_BACKEND='${SR_GPU_BACKEND}'. Use auto, cuda, rocm, intel, cpu, or none.")
endif()

# Honour a declined backend: fall through to the CPU strategy and correct the
# resolved value, so the CLI is never told 'cuda' about a build that has no
# offload triple in it. Only reachable when the backend was guessed
# (SR_GPU_BACKEND_EXPLICIT is FALSE); a named one fails in its own file instead
# of being quietly downgraded.
if(SR_GPU_BACKEND_DECLINED)
    set(_sr_backend "cpu")
    set(SR_GPU_BACKEND_RESOLVED "cpu" CACHE INTERNAL "Resolved GPU backend" FORCE)
    include("${CMAKE_CURRENT_LIST_DIR}/CpuFallback.cmake")
endif()
