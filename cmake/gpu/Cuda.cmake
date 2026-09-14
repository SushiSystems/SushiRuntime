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
            # "\\." and not "\.": inside a quoted argument CMake reads "\." as an
            # escape for the character '.', so the regex engine is handed a bare
            # dot that matches anything. It cost nothing here only by luck --
            # "8x6" matched and yielded 86, and the ^[0-9]+$ check downstream saw
            # a clean pair of digits because the separator had been dropped on
            # the way. The escaped form reaches the engine as a literal dot.
            if(_sr_line MATCHES "^([0-9]+)\\.([0-9]+)$")
                list(APPEND _sr_caps "${CMAKE_MATCH_1}${CMAKE_MATCH_2}")
            endif()
        endforeach()
        list(REMOVE_DUPLICATES _sr_caps)
    endif()

    sushiruntime_gpu_resolve_arch(SR_CUDA_ARCH _sr_caps
        "cuda" "nvidia-smi" "compute capability"
        "-DSR_CUDA_ARCH=61 (Pascal), 86 (Ampere), 89 (Ada)")
endif()

sushiruntime_gpu_publish_arch(SR_CUDA_ARCH
    "a compute capability"
    "^[0-9]+$"
    "Use the digits only, without the dot or an sm_ prefix: 86, not 8.6 or sm_86."
    "CUDA compute capabilities for the SYCL nvptx64 backend. One value (86) or a list for a multi-arch binary (61;86). No default: it is resolved from nvidia-smi or stated explicitly.")

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
    # The located root is passed only where it holds libdevice, so an apt layout keeps
    # the driver's own detection.
    if(SR_CUDA_TOOLKIT_ROOT AND EXISTS "${SR_CUDA_TOOLKIT_ROOT}/nvvm/libdevice")
        sushiruntime_gpu_add_clang_option("--cuda-path=${SR_CUDA_TOOLKIT_ROOT}")
    endif()
endif()
