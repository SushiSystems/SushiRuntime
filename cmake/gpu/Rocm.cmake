# gpu/Rocm.cmake — AMD ROCm (amdgcn) SYCL backend strategy.
#
# Owns all knowledge of how to target AMD GPUs, including resolving which ISAs to
# build for. Declares the result through the GpuBackend primitives. Only included
# when DetectGpu.cmake resolves the ROCm backend, so the hard failures below can
# never fire on a CPU/Intel/CUDA build.
#
# SR_AMD_ARCH has NO default, for the same reason SR_CUDA_ARCH has none (see
# gpu/Cuda.cmake): a default is a guess about hardware nobody looked at. AMD is
# harsher about it than NVIDIA — amdgcn offload is compiled to final ISA with no
# forward-compatible intermediate, so a wrong arch does not merely lose codegen
# quality, it produces a binary the target GPU cannot run at all. Either the
# hardware's own answer is taken whole: several different GPUs means several
# targets, not an election between them.

if(NOT DEFINED SR_AMD_ARCH OR SR_AMD_ARCH STREQUAL "")
    set(_sr_gfx "")
    # rocm_agent_enumerator is the purpose-built probe: one agent per line, no
    # parsing of rocminfo's prose layout. It ships with ROCm but does not put
    # itself on PATH, hence the well-known prefixes DetectGpu.cmake also uses.
    sushiruntime_gpu_versioned_prefixes(_sr_rocm_prefixes "/opt/rocm-*/bin")
    find_program(_sr_rocm_enum rocm_agent_enumerator
        PATHS /opt/rocm/bin ${_sr_rocm_prefixes})
    if(_sr_rocm_enum)
        execute_process(
            COMMAND ${_sr_rocm_enum}
            OUTPUT_VARIABLE _sr_agents
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        string(REPLACE "\n" ";" _sr_agent_lines "${_sr_agents}")
        foreach(_sr_line IN LISTS _sr_agent_lines)
            string(STRIP "${_sr_line}" _sr_line)
            # gfx000 is the host CPU agent every ROCm install reports; it is not a
            # GPU and must not be mistaken for one.
            if(_sr_line MATCHES "^gfx[0-9a-f]+$" AND NOT _sr_line STREQUAL "gfx000")
                list(APPEND _sr_gfx "${_sr_line}")
            endif()
        endforeach()
        list(REMOVE_DUPLICATES _sr_gfx)
    endif()

    sushiruntime_gpu_resolve_arch(SR_AMD_ARCH _sr_gfx
        "rocm" "rocm_agent_enumerator" "GPU ISA"
        "-DSR_AMD_ARCH=gfx1030 (RDNA2), gfx1100 (RDNA3), gfx906 (Vega20)")
endif()

sushiruntime_gpu_publish_arch(SR_AMD_ARCH
    "an AMD GPU ISA"
    "^gfx[0-9a-f]+$"
    "Use the gfx name as ROCm reports it: gfx1030, not 1030 or RDNA2."
    "AMD GPU ISAs for the SYCL amdgcn backend. One value (gfx1030) or a list for a multi-arch binary (gfx1030;gfx1100). No default: it is resolved from rocm_agent_enumerator or stated explicitly.")

string(REPLACE ";" ", " _sr_arch_report "${SR_AMD_ARCH}")
message(STATUS "GPU backend: AMD ROCm (amdgcn-amd-amdhsa ${_sr_arch_report})")

if(SR_SYCL_TOOLCHAIN STREQUAL "adaptivecpp")
    # AdaptiveCpp lists several devices of one backend inside a single target
    # spec: hip:gfx1030,gfx1100 — not one hip: entry per ISA, which it rejects as
    # a duplicate backend.
    string(REPLACE ";" "," _sr_acpp_devices "${SR_AMD_ARCH}")
    sushiruntime_gpu_add_acpp_target("hip:${_sr_acpp_devices}")
else()
    # The clang toolchains take the ISA as a -fsycl-targets alias
    # (amd_gpu_gfx1030) rather than the bare amdgcn-amd-amdhsa triple plus a
    # -Xsycl-target-backend --offload-arch=. The alias form is what carries more
    # than one ISA in a single binary, one alias per architecture, and it mirrors
    # what gpu/Cuda.cmake does for NVIDIA.
    #
    # UNVERIFIED: multi-ISA AMD offload has not been built or run on this project.
    # The single-ISA path is the tested one; a list is expected to work by
    # symmetry with the NVIDIA aliases but has no evidence behind it yet.
    foreach(_sr_arch IN LISTS SR_AMD_ARCH)
        sushiruntime_gpu_add_clang_triple("amd_gpu_${_sr_arch}")
    endforeach()
endif()
