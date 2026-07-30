# gpu/Rocm.cmake — AMD ROCm (amdgcn) SYCL backend strategy.
#
# Owns all knowledge of how to target AMD GPUs; declares it through the GpuBackend
# primitives. Arch comes from SR_AMD_ARCH (default gfx1030 = RDNA2). AMD offload
# needs an -fsycl-targets triple plus an --offload-arch selecting the exact ISA.

message(STATUS "GPU backend: AMD ROCm (amdgcn-amd-amdhsa ${SR_AMD_ARCH})")

if(SR_SYCL_TOOLCHAIN STREQUAL "adaptivecpp")
    sushiruntime_gpu_add_acpp_target("hip:${SR_AMD_ARCH}")
else()
    sushiruntime_gpu_add_clang_triple("amdgcn-amd-amdhsa")
    sushiruntime_gpu_add_clang_option(
        -Xsycl-target-backend=amdgcn-amd-amdhsa --offload-arch=${SR_AMD_ARCH})
endif()
