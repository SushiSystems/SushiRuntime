# CpuFallback.cmake
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026 Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

# gpu/CpuFallback.cmake — portable CPU / OpenCL SYCL backend strategy.
#
# The no-accelerator path. spir64 (JIT) is already seeded by GpuBackend and runs on
# the OpenCL CPU device and any SPIR-V-capable runtime, so there is nothing to add
# here. This file exists so the dispatch in DetectGpu.cmake is total and the "no
# GPU" case is an explicit, named strategy rather than a silent else-branch.

message(STATUS "GPU backend: CPU / OpenCL (spir64 JIT) — no accelerator toolkit detected.")
