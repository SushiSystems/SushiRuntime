# gpu/Cuda.cmake — NVIDIA CUDA (nvptx64) SYCL backend strategy.
#
# Owns all knowledge of how to target NVIDIA GPUs; declares it through the
# GpuBackend primitives. Arch comes from SR_CUDA_ARCH (default 61 = Pascal).

message(STATUS "GPU backend: NVIDIA CUDA (nvptx64 sm_${SR_CUDA_ARCH})")

if(SR_SYCL_TOOLCHAIN STREQUAL "adaptivecpp")
    sushi_gpu_add_acpp_target("cuda:sm_${SR_CUDA_ARCH}")
else()
    sushi_gpu_add_clang_triple("nvptx64-nvidia-cuda")
    sushi_gpu_add_clang_option(
        -Xsycl-target-backend=nvptx64-nvidia-cuda --cuda-gpu-arch=sm_${SR_CUDA_ARCH})
endif()
