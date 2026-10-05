# RocmToolkit.cmake
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026 Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

# gpu/locate/RocmToolkit.cmake: where the AMD ROCm toolkit is installed.
#
# One function per vendor, one branch per operating system, so a platform is added
# inside this file and nowhere else. The design is SushiStack's
# docs/design/GPU_BACKEND_PROVISIONING.md §4.

# sushiruntime_gpu_locate_rocm_toolkit(<out_root> <out_searched>)
#   Sets <out_root> to the ROCm install root, or to "" when none is found, and
#   <out_searched> to what was looked at.
function(sushiruntime_gpu_locate_rocm_toolkit out_root out_searched)
    set(_root "")
    if(SR_HIPCC)
        set(_searched "SR_HIPCC")
        get_filename_component(_bin "${SR_HIPCC}" DIRECTORY)
        get_filename_component(_root "${_bin}" DIRECTORY)
    elseif(WIN32)
        set(_searched "nothing: no ROCm locator is provided on Windows")
    else()
        set(_searched "hipcc or rocminfo on PATH, /opt/rocm/bin, /opt/rocm-*/bin")
        sushiruntime_gpu_versioned_prefixes(_prefixes "/opt/rocm-*/bin")
        find_program(SR_HIPCC NAMES hipcc rocminfo PATHS /opt/rocm/bin ${_prefixes})
        if(SR_HIPCC)
            get_filename_component(_bin "${SR_HIPCC}" DIRECTORY)
            get_filename_component(_root "${_bin}" DIRECTORY)
        endif()
    endif()
    set(${out_root} "${_root}" PARENT_SCOPE)
    set(${out_searched} "${_searched}" PARENT_SCOPE)
endfunction()
