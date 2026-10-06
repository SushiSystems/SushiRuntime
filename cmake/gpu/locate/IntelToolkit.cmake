# IntelToolkit.cmake
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

# gpu/locate/IntelToolkit.cmake: where an Intel GPU toolkit is installed.
#
# Intel is never auto-detected: spir64 already runs on Intel GPUs, and the only
# thing gpu/IntelGpu.cmake adds is AOT for a device that has to be named (see
# DetectGpu.cmake). The function exists so every vendor answers the same call.

# sushiruntime_gpu_locate_intel_toolkit(<out_root> <out_searched>)
#   Sets <out_root> to "", because Intel GPUs are reached by request rather than detection.
function(sushiruntime_gpu_locate_intel_toolkit out_root out_searched)
    set(${out_root} "" PARENT_SCOPE)
    set(${out_searched} "nothing: Intel GPUs are requested, not detected" PARENT_SCOPE)
endfunction()
