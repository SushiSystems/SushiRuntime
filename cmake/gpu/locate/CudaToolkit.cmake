# CudaToolkit.cmake
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

# gpu/locate/CudaToolkit.cmake: where the NVIDIA CUDA toolkit is installed.
#
# One function per vendor, one branch per operating system, so a platform is added
# inside this file and nowhere else. The design is SushiStack's
# docs/design/GPU_BACKEND_PROVISIONING.md §4.

# sushiruntime_gpu_locate_cuda_toolkit(<out_root> <out_searched>)
#   Sets <out_root> to the CUDA toolkit's install root, or to "" when none is found, and
#   <out_searched> to what was looked at.
function(sushiruntime_gpu_locate_cuda_toolkit out_root out_searched)
    set(_root "")
    if(SR_NVCC)
        set(_searched "SR_NVCC")
        get_filename_component(_bin "${SR_NVCC}" DIRECTORY)
        get_filename_component(_root "${_bin}" DIRECTORY)
    elseif(WIN32)
        set(_searched "CUDA_PATH in the process environment and the machine registry (bin/nvcc.exe)")
        set(_candidates "$ENV{CUDA_PATH}")
        if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
            cmake_host_system_information(RESULT _machine_cuda_path QUERY WINDOWS_REGISTRY
                "HKLM/SYSTEM/CurrentControlSet/Control/Session Manager/Environment"
                VALUE CUDA_PATH)
            list(APPEND _candidates "${_machine_cuda_path}")
        endif()
        foreach(_candidate IN LISTS _candidates)
            if(_candidate)
                file(TO_CMAKE_PATH "${_candidate}" _candidate)
                if(EXISTS "${_candidate}/bin/nvcc.exe")
                    set(_root "${_candidate}")
                    break()
                endif()
            endif()
        endforeach()
    elseif(EXISTS "$ENV{CUDA_PATH}/bin/nvcc")
        # sushicore installs the release chosen for the machine's cards under the user's
        # home and names it in CUDA_PATH, so it wins over any system install.
        set(_searched "CUDA_PATH (bin/nvcc)")
        file(TO_CMAKE_PATH "$ENV{CUDA_PATH}" _root)
    else()
        set(_searched "CUDA_PATH, nvcc on PATH, /usr/local/cuda/bin, /usr/local/cuda-*/bin")
        sushiruntime_gpu_versioned_prefixes(_prefixes "/usr/local/cuda-*/bin")
        find_program(SR_NVCC nvcc PATHS /usr/local/cuda/bin ${_prefixes})
        if(SR_NVCC)
            get_filename_component(_bin "${SR_NVCC}" DIRECTORY)
            get_filename_component(_root "${_bin}" DIRECTORY)
        endif()
    endif()
    set(${out_root} "${_root}" PARENT_SCOPE)
    set(${out_searched} "${_searched}" PARENT_SCOPE)
endfunction()
