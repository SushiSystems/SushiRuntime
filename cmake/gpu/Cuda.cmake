# gpu/Cuda.cmake — NVIDIA CUDA (nvptx64) SYCL backend strategy.
#
# Owns all knowledge of how to target NVIDIA GPUs, including resolving which
# compute capabilities to build for. Declares the result through the GpuBackend
# primitives. Only included when DetectGpu.cmake resolves the CUDA backend, so
# the hard failures below can never fire on a CPU/Intel/ROCm build.
#
# SR_CUDA_ARCH has NO default. A default here would be a guess about hardware
# nobody looked at, and a wrong guess is invisible: PTX is forward-compatible, so
# an sm_61 binary still runs on Ampere — just without Ampere codegen, and with
# nothing in the log to say so. What replaces the default is the hardware's own
# answer, taken whole.
#
# "Taken whole" is what decides the multi-GPU case. A box with a Pascal and an
# Ampere card has no single right target, but it does have a right *answer*:
# both. Building for every capability reported invents nothing and discards
# nothing, so it is not the guess this file exists to refuse — electing whichever
# one nvidia-smi listed first would have been. The cost is real and visible:
# compile time and binary size scale with the number of architectures, and the
# STATUS line says so along with the flag that narrows it.
#
# Resolving the arch does not guarantee the toolkit supports it: NVIDIA drops
# architectures from ptxas across major releases (CUDA 13 removed sm_6x/Pascal
# outright). That failure still surfaces at the ptxas link step and needs a
# compatible toolkit — see cli/sushistack/setup/package_managers.py::
# ensure_cuda_toolkit, which pins CUDA 12.6 precisely to keep Pascal buildable.

if(NOT DEFINED SR_CUDA_ARCH OR SR_CUDA_ARCH STREQUAL "")
    set(_sr_caps "")
    find_program(_sr_nvidia_smi nvidia-smi)
    if(_sr_nvidia_smi)
        execute_process(
            COMMAND ${_sr_nvidia_smi} --query-gpu=compute_cap --format=csv,noheader
            OUTPUT_VARIABLE _sr_compute_cap
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        # One line per GPU. Collect every one, then collapse duplicates: two
        # identical cards are not an ambiguity, two different ones are.
        string(REPLACE "\n" ";" _sr_cap_lines "${_sr_compute_cap}")
        foreach(_sr_line IN LISTS _sr_cap_lines)
            string(STRIP "${_sr_line}" _sr_line)
            if(_sr_line MATCHES "^([0-9]+)\.([0-9]+)$")
                list(APPEND _sr_caps "${CMAKE_MATCH_1}${CMAKE_MATCH_2}")
            endif()
        endforeach()
        list(REMOVE_DUPLICATES _sr_caps)
    endif()

    list(LENGTH _sr_caps _sr_cap_count)
    if(_sr_cap_count EQUAL 1)
        set(SR_CUDA_ARCH "${_sr_caps}")
        message(STATUS "GPU auto-detect: nvidia-smi reports one compute capability "
                       "-> SR_CUDA_ARCH=${SR_CUDA_ARCH}")
    elseif(_sr_cap_count EQUAL 0)
        # No GPU answered. How loudly that fails depends on who asked for CUDA.
        # auto-detect selected this backend by finding nvcc, which says a toolkit
        # is installed, not that a card is present -- the usual shape of a CI or
        # artifact build box. Killing the build there would make "auto" a mode
        # that fails on a machine it was supposed to figure out, so it declines
        # and DetectGpu.cmake demotes to the CPU strategy. An explicit
        # -DSR_GPU_BACKEND=cuda is a stated intent, and stating it without an
        # architecture is the omission this whole file exists to refuse.
        if(NOT SR_GPU_BACKEND_EXPLICIT)
            message(WARNING
                "CUDA was auto-selected because a CUDA toolkit is installed, but "
                "nvidia-smi reports no GPU, so SR_CUDA_ARCH cannot be resolved. "
                "Falling back to the CPU/OpenCL path."
                " To build CUDA offload on a machine with no NVIDIA GPU, state "
                "both explicitly: -DSR_GPU_BACKEND=cuda -DSR_CUDA_ARCH=61")
            set(SR_GPU_BACKEND_DECLINED TRUE)
            return()
        endif()
        message(FATAL_ERROR
            "SR_CUDA_ARCH could not be resolved: nvidia-smi is unavailable or "
            "reported no compute capability, and there is no default to fall "
            "back on.\n"
            "Pass the target architecture explicitly, e.g. -DSR_CUDA_ARCH=61 "
            "(Pascal), 86 (Ampere), 89 (Ada). A build machine with no NVIDIA GPU "
            "must always state its target this way.")
    else()
        # Several different cards. Target all of them rather than electing one:
        # the set is the honest reading of what is installed. Announced loudly,
        # because the build just got longer by a factor nobody typed.
        string(REPLACE ";" ", " _sr_cap_list "${_sr_caps}")
        set(SR_CUDA_ARCH "${_sr_caps}")
        message(STATUS "GPU auto-detect: nvidia-smi reports ${_sr_cap_count} "
                       "compute capabilities (${_sr_cap_list}); building for all "
                       "of them. Narrow with -DSR_CUDA_ARCH=<cc> to cut build "
                       "time and binary size.")
    endif()
endif()

set(SR_CUDA_ARCH "${SR_CUDA_ARCH}" CACHE STRING
    "CUDA compute capabilities for the SYCL nvptx64 backend. One value (86) or a "
    "list for a multi-arch binary (61;86). No default: it is resolved from "
    "nvidia-smi or stated explicitly.")

foreach(_sr_arch IN LISTS SR_CUDA_ARCH)
    if(NOT _sr_arch MATCHES "^[0-9]+$")
        message(FATAL_ERROR
            "SR_CUDA_ARCH contains '${_sr_arch}', which is not a compute "
            "capability. Use the digits only, without the dot or an sm_ prefix: "
            "86, not 8.6 or sm_86.")
    endif()
endforeach()

string(REPLACE ";" ", " _sr_arch_report "${SR_CUDA_ARCH}")
message(STATUS "GPU backend: NVIDIA CUDA (nvptx64 sm_${_sr_arch_report})")

if(SR_SYCL_TOOLCHAIN STREQUAL "adaptivecpp")
    # AdaptiveCpp lists several devices of one backend inside a single target
    # spec: cuda:sm_61,sm_86 — not one cuda: entry per arch, which it rejects
    # as a duplicate backend.
    set(_sr_acpp_devices "")
    foreach(_sr_arch IN LISTS SR_CUDA_ARCH)
        list(APPEND _sr_acpp_devices "sm_${_sr_arch}")
    endforeach()
    string(REPLACE ";" "," _sr_acpp_devices "${_sr_acpp_devices}")
    sushiruntime_gpu_add_acpp_target("cuda:${_sr_acpp_devices}")
else()
    # The clang toolchains take the architecture as a -fsycl-targets alias
    # (nvidia_gpu_sm_86) rather than the bare nvptx64-nvidia-cuda triple plus a
    # -Xsycl-target-backend --cuda-gpu-arch=. The alias form is what supports
    # more than one NVIDIA arch in a single binary: --cuda-gpu-arch cannot be
    # repeated, but the target list can carry one alias per architecture.
    foreach(_sr_arch IN LISTS SR_CUDA_ARCH)
        sushiruntime_gpu_add_clang_triple("nvidia_gpu_sm_${_sr_arch}")
    endforeach()
endif()
