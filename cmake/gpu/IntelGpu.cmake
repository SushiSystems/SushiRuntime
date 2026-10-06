# IntelGpu.cmake
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

# gpu/IntelGpu.cmake — Intel GPU SYCL backend strategy.
#
# Owns all knowledge of how to target Intel GPUs. spir64 (already seeded by
# GpuBackend) is the portable JIT path that runs on Intel GPUs out of the box, so
# the default Intel backend adds nothing extra. When SR_INTEL_GPU_DEVICE is set,
# it switches to ahead-of-time compilation via the spir64_gen triple for that
# specific device (e.g. pvc, dg2, acm-g10), which removes the first-run JIT cost.

if(SR_SYCL_TOOLCHAIN STREQUAL "adaptivecpp")
    # AdaptiveCpp reaches Intel GPUs through its generic (JIT) target, already
    # seeded — nothing to add.
    message(STATUS "GPU backend: Intel GPU via AdaptiveCpp generic (JIT).")
elseif(SR_INTEL_GPU_DEVICE STREQUAL "")
    message(STATUS "GPU backend: Intel GPU via spir64 (JIT).")
else()
    message(STATUS "GPU backend: Intel GPU AOT (spir64_gen -device ${SR_INTEL_GPU_DEVICE})")
    sushiruntime_gpu_add_clang_triple("spir64_gen")
    sushiruntime_gpu_add_clang_option(
        -Xsycl-target-backend=spir64_gen "-device ${SR_INTEL_GPU_DEVICE}")
endif()
