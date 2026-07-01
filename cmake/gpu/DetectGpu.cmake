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

if(_sr_backend STREQUAL "auto")
    # The SYCL CUDA/nvptx and amdgcn passes need the vendor toolkit's device
    # libraries (libdevice / ROCm device libs), so a mere driver is not enough:
    # probe for the *compiler* toolchain, not the runtime.
    find_program(SR_NVCC nvcc)
    find_program(SR_HIPCC NAMES hipcc rocminfo)
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
if(_sr_backend STREQUAL "cuda")
    include(gpu/Cuda)
elseif(_sr_backend STREQUAL "rocm")
    include(gpu/Rocm)
elseif(_sr_backend STREQUAL "intel")
    include(gpu/IntelGpu)
elseif(_sr_backend STREQUAL "cpu" OR _sr_backend STREQUAL "none")
    include(gpu/CpuFallback)
else()
    message(FATAL_ERROR "Unknown SR_GPU_BACKEND='${SR_GPU_BACKEND}'. Use auto, cuda, rocm, intel, cpu, or none.")
endif()
