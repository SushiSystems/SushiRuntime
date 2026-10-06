# DetectGpu.cmake
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

# gpu/DetectGpu.cmake — resolve which GPU backends to build for.
#
# Single responsibility: turn the SR_GPU_BACKEND request into the concrete set of
# vendor backends this build offloads to, and include each one's strategy file.
# This is the single source of truth for GPU selection — the CLI does not
# duplicate the toolkit check in Python, it just passes SR_GPU_BACKEND.
#
# A set, not an election. gpu/Cuda.cmake argues that a box with a Pascal and an
# Ampere card has no single right target but does have a right answer — both —
# and that picking whichever one nvidia-smi listed first would be exactly the
# guess it exists to refuse. That argument does not stop at the vendor boundary.
# A box with an NVIDIA card and an AMD card is the same shape of question, so it
# gets the same shape of answer: every vendor that reports hardware is targeted,
# and the accumulators in gpu/GpuBackend.cmake carry all of them, because a triple
# list was always additive. This file used to answer it by election instead --
# an if/elseif chain that found nvcc and never looked for ROCm -- which was the
# one place the codebase contradicted its own doctrine.
#
# Precedence (first that applies wins):
#   1. explicit SR_GPU_BACKEND != auto     (a vendor list, or cpu / none)
#   2. legacy SR_ENABLE_CUDA=ON            -> cuda   (back-compat)
#   3. auto-detect from the host toolchains, else cpu
#
# UNVERIFIED, and the reason the set is worth having is also the reason to say so:
# a cross-vendor build has never been compiled or run on this project. What was
# measured is that intel/llvm's driver accepts spir64, nvidia_gpu_sm_89 and
# amd_gpu_gfx1030 in one -fsycl-targets invocation and walks both device paths,
# failing only for want of ROCm's device libraries on a machine that has none. So
# cross-vendor offload is a device-library question rather than a driver one. What
# remains unmeasured is the link step that bundles two offload images into one
# binary, and whether both adapters load and enumerate at runtime.

set(SR_GPU_BACKEND "auto" CACHE STRING
    "GPU compute backends: auto (detect), a list of cuda/rocm/intel, or cpu / none")
set_property(CACHE SR_GPU_BACKEND PROPERTY STRINGS auto cuda rocm intel cpu none)

# The vendors that compose. cpu and none are not on this list because they are not
# vendors: they name the absence of an accelerator, which is a statement about the
# whole build rather than a member of a set (see the rejection below).
set(SR_GPU_VENDORS cuda rocm intel)

# Every vendor answers where its toolkit lives through one function in gpu/locate/,
# with the operating-system branches inside it. The roots are published for the
# vendor strategy files, which need them for flags such as intel/llvm's --cuda-path.
foreach(_sr_vendor IN LISTS SR_GPU_VENDORS)
    string(TOUPPER "${_sr_vendor}" _sr_vendor_upper)
    string(SUBSTRING "${_sr_vendor_upper}" 0 1 _sr_vendor_initial)
    string(SUBSTRING "${_sr_vendor}" 1 -1 _sr_vendor_rest)
    include("${CMAKE_CURRENT_LIST_DIR}/locate/${_sr_vendor_initial}${_sr_vendor_rest}Toolkit.cmake" OPTIONAL
        RESULT_VARIABLE _sr_locator_file)
    if(NOT _sr_locator_file)
        message(FATAL_ERROR "gpu/locate/ has no toolkit locator for vendor '${_sr_vendor}'.")
    endif()
    cmake_language(CALL sushiruntime_gpu_locate_${_sr_vendor}_toolkit
        _sr_toolkit_root _sr_toolkit_searched)
    set(SR_${_sr_vendor_upper}_TOOLKIT_ROOT "${_sr_toolkit_root}")
    list(APPEND _sr_toolkit_search_report "${_sr_vendor}: ${_sr_toolkit_searched}")
endforeach()
unset(_sr_vendor_initial)
unset(_sr_vendor_rest)
unset(_sr_locator_file)
unset(_sr_toolkit_root)
unset(_sr_toolkit_searched)

set(_sr_request "${SR_GPU_BACKEND}")

# Back-compat: honor the old -DSR_ENABLE_CUDA=ON switch when no explicit backend
# was chosen. Defaulted OFF so it never forces CPU over an auto-detected GPU.
option(SR_ENABLE_CUDA "Legacy switch: force the SYCL CUDA (nvptx64) backend" OFF)
if(_sr_request STREQUAL "auto" AND SR_ENABLE_CUDA)
    set(_sr_request "cuda")
endif()

# Did a human name these backends, or did we guess them? A vendor file that finds
# its toolkit installed but no device consults this to choose between failing and
# declining. One flag covers the whole set rather than one per vendor, because the
# request has a single origin: either every vendor in it was typed, or every vendor
# in it was detected. SR_ENABLE_CUDA counts as naming one -- it is the legacy
# spelling of the same request, and the rewrite above happens first, so it reaches
# the else branch and is not downgraded to a guess.
if(_sr_request STREQUAL "auto")
    set(SR_GPU_BACKEND_EXPLICIT FALSE)
else()
    set(SR_GPU_BACKEND_EXPLICIT TRUE)
endif()

if(_sr_request STREQUAL "auto")
    # The SYCL CUDA/nvptx and amdgcn passes need the vendor toolkit's device
    # libraries, so a mere driver is not enough: a vendor is requested when its
    # locator in gpu/locate/ found the toolkit, on whichever operating system.
    #
    # Every toolkit found is requested, not the first one. Finding a toolkit still
    # proves only that a compiler is installed, so each vendor file runs its own
    # hardware probe and declines when nothing answers; the loop below drops the
    # ones that do. That is what keeps "both toolkits installed, one card present"
    # from building for a GPU that is not there.
    #
    # Intel is not probed here, and its absence is a decision rather than a gap:
    # spir64 is seeded unconditionally by gpu/GpuBackend.cmake and already runs on
    # Intel GPUs, so an auto-selected Intel backend would add nothing. The only
    # thing gpu/IntelGpu.cmake adds is AOT for a named device, and naming it is a
    # choice a probe cannot make -- which is why it is reached by asking.
    set(_sr_request "")

    foreach(_sr_vendor IN LISTS SR_GPU_VENDORS)
        string(TOUPPER "${_sr_vendor}" _sr_vendor_upper)
        if(SR_${_sr_vendor_upper}_TOOLKIT_ROOT)
            list(APPEND _sr_request "${_sr_vendor}")
        endif()
    endforeach()

    if(_sr_request)
        string(REPLACE ";" ", " _sr_request_report "${_sr_request}")
        message(STATUS "GPU auto-detect: accelerator toolkits found for ${_sr_request_report}.")
    else()
        # No accelerator toolkit — build the portable CPU/OpenCL (spir64) path.
        set(_sr_request "cpu")
        string(REPLACE ";" "; " _sr_search_report "${_sr_toolkit_search_report}")
        message(STATUS "GPU auto-detect: no accelerator toolkit found (searched ${_sr_search_report}); selected 'cpu'.")
    endif()
endif()

# cpu and none are terminal: they say this build offloads to no accelerator, which
# nothing can be added to. Handling them before the set is validated is what lets
# the validation treat every remaining entry as a vendor.
if(_sr_request STREQUAL "cpu" OR _sr_request STREQUAL "none")
    include("${CMAKE_CURRENT_LIST_DIR}/CpuFallback.cmake")
    set(SR_GPU_BACKEND_RESOLVED "${_sr_request}" CACHE INTERNAL "Resolved GPU backends")
    return()
endif()

foreach(_sr_entry IN LISTS _sr_request)
    if(_sr_entry STREQUAL "cpu" OR _sr_entry STREQUAL "none")
        message(FATAL_ERROR
            "SR_GPU_BACKEND='${SR_GPU_BACKEND}' combines '${_sr_entry}' with a vendor. "
            "'${_sr_entry}' is not one backend among others -- it states that this build "
            "offloads to no accelerator, so there is nothing for it to be combined with. "
            "The portable spir64 path is present in every build regardless; drop "
            "'${_sr_entry}' from the list to keep the accelerators, or make it the whole "
            "value to drop them.")
    elseif(NOT _sr_entry IN_LIST SR_GPU_VENDORS)
        string(REPLACE ";" ", " _sr_vendor_report "${SR_GPU_VENDORS}")
        message(FATAL_ERROR
            "SR_GPU_BACKEND='${SR_GPU_BACKEND}' names '${_sr_entry}', which is not a GPU "
            "backend. Use auto, cpu, none, or a list of: ${_sr_vendor_report}.")
    endif()
endforeach()

# Dispatch to each vendor strategy. Every file is self-contained and only appends
# to the GpuBackend accumulators; adding a vendor means adding a branch here, a
# strategy file, a toolkit locator in gpu/locate/, and an entry in SR_GPU_VENDORS above.
#
# A vendor may also decline. Selecting it proves a toolkit is installed and not
# that a device exists, so a backend can discover at include time that it has no
# hardware to target. Rather than resolve that itself it sets
# SR_GPU_BACKEND_DECLINED and returns, which exits that file alone and leaves this
# loop running -- so one vendor declining costs the others nothing. This file stays
# the single writer of SR_GPU_BACKEND_RESOLVED.
#
# The loop variable is read again after the include, and include() shares this
# scope, so it is named for this loop alone: a vendor file that happened to write
# _sr_entry would otherwise rename the backend that just ran.
set(_sr_resolved "")

foreach(_sr_entry IN LISTS _sr_request)
    set(SR_GPU_BACKEND_DECLINED FALSE)

    if(_sr_entry STREQUAL "cuda")
        include("${CMAKE_CURRENT_LIST_DIR}/Cuda.cmake")
    elseif(_sr_entry STREQUAL "rocm")
        include("${CMAKE_CURRENT_LIST_DIR}/Rocm.cmake")
    elseif(_sr_entry STREQUAL "intel")
        include("${CMAKE_CURRENT_LIST_DIR}/IntelGpu.cmake")
    endif()

    if(NOT SR_GPU_BACKEND_DECLINED)
        list(APPEND _sr_resolved "${_sr_entry}")
    endif()
endforeach()

# Every vendor declined, so this build has no offload in it after all. Correcting
# the resolved value matters as much as the fallback: nothing downstream should be
# told 'cuda' about a build carrying no CUDA triple. Only reachable when the set
# was guessed -- a named vendor fails inside its own file rather than being quietly
# downgraded.
if(NOT _sr_resolved)
    set(_sr_resolved "cpu")
    include("${CMAKE_CURRENT_LIST_DIR}/CpuFallback.cmake")
endif()

set(SR_GPU_BACKEND_RESOLVED "${_sr_resolved}" CACHE INTERNAL "Resolved GPU backends")
