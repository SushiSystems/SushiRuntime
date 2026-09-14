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
        set(_searched "CUDA_PATH (bin/nvcc.exe)")
        if(DEFINED ENV{CUDA_PATH})
            file(TO_CMAKE_PATH "$ENV{CUDA_PATH}" _candidate)
            if(EXISTS "${_candidate}/bin/nvcc.exe")
                set(_root "${_candidate}")
            endif()
        endif()
    else()
        set(_searched "nvcc on PATH, /usr/local/cuda/bin, /usr/local/cuda-*/bin")
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
