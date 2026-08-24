# gpu/GpuBackend.cmake — the GPU backend mechanism (Strategy pattern core).
#
# Single responsibility: provide the vocabulary a vendor backend uses to declare
# how it offloads, and one finalize step that renders those declarations into the
# actual compiler/linker flags for the active SYCL toolchain. It knows NOTHING
# about any specific vendor — CUDA/ROCm/Intel/CPU each live in their own file and
# call the primitives below. Adding a vendor never touches this file (Open-Closed).
#
# Two rendering targets, because the toolchains speak different dialects:
#   * clang-based (intel-llvm, oneapi): a single combined
#         -fsycl-targets=spir64,<accel-triples...>
#     plus per-triple -Xsycl-target-backend options.
#   * AdaptiveCpp (acpp): an ACPP_TARGETS list (generic;cuda:sm_XX;hip:gfxXXX).
#
# Accumulators (plain directory-scope vars; backend files are include()d into this
# same scope, so their appends are visible here at finalize time):
#   SR_GPU_CLANG_TRIPLES  seeded with spir64 (JIT CPU + Intel GPU, always valid on
#                         the clang toolchains); accelerators append their triple.
#   SR_GPU_CLANG_OPTIONS  extra -Xsycl-target-backend ... flags.
#   SR_GPU_ACPP_TARGETS   seeded with generic; accelerators append their target.
#   SR_GPU_OFFLOAD_ADDED  TRUE once a real accelerator triple was added — gates
#                         whether the clang path emits -fsycl-targets at all (with
#                         only spir64 we emit nothing, preserving JIT-default and
#                         today's exact Windows/CPU behavior).

set(SR_GPU_CLANG_TRIPLES "spir64")
set(SR_GPU_CLANG_OPTIONS "")
set(SR_GPU_ACPP_TARGETS "generic")
set(SR_GPU_OFFLOAD_ADDED FALSE)

# Per-vendor arch knobs. Each backend reads its own; unrelated backends ignore
# theirs.
#
# SR_CUDA_ARCH and SR_AMD_ARCH are deliberately NOT declared here. Resolving
# either means probing one vendor's hardware and failing when the answer is
# ambiguous, and this file must stay vendor-agnostic (see the header). More
# importantly, a hard failure here would fire on every unrelated build, since
# this file is included unconditionally. Each lives in its own backend file --
# gpu/Cuda.cmake and gpu/Rocm.cmake -- which DetectGpu.cmake includes only when
# that backend is actually selected.
#
# SR_INTEL_GPU_DEVICE stays here and keeps its empty default because empty is not
# a guess: it is the documented request for JIT spir64, which is a real choice
# rather than a stand-in for hardware nobody looked at.
set(SR_INTEL_GPU_DEVICE "" CACHE STRING
    "Intel GPU AOT device for spir64_gen (e.g. pvc, dg2, acm-g10); empty => JIT spir64")

# --- Backend-facing primitives -------------------------------------------------

# Register an accelerator target for the clang toolchains. This is whatever
# -fsycl-targets accepts: a bare triple (amdgcn-amd-amdhsa) or an architecture
# alias (nvidia_gpu_sm_86, amd_gpu_gfx1030). The CUDA and ROCm backends use the
# alias form, because that is what lets one binary carry several architectures --
# the per-triple --cuda-gpu-arch / --offload-arch options cannot be repeated.
# Marks that real offload is present.
macro(sushiruntime_gpu_add_clang_triple _triple)
    list(APPEND SR_GPU_CLANG_TRIPLES "${_triple}")
    set(SR_GPU_OFFLOAD_ADDED TRUE)
endmacro()

# Append raw backend flags (e.g. -Xsycl-target-backend=... --offload-arch=...).
macro(sushiruntime_gpu_add_clang_option)
    list(APPEND SR_GPU_CLANG_OPTIONS ${ARGN})
endmacro()

# Register an AdaptiveCpp target (e.g. cuda:sm_86, hip:gfx1030).
macro(sushiruntime_gpu_add_acpp_target _target)
    list(APPEND SR_GPU_ACPP_TARGETS "${_target}")
    set(SR_GPU_OFFLOAD_ADDED TRUE)
endmacro()

# --- Finalize: render accumulators into flags ----------------------------------
#
# A macro so the directory-scope add_compile_options/add_link_options land in the
# caller's (root) scope and cascade to add_subdirectory() exactly like the former
# global flags. Applying GPU device flags at directory scope — rather than via an
# INTERFACE target — is deliberate: the clang -fsycl device pass and
# add_sycl_to_target() operate on directory/source scope today, and reproducing
# that exactly avoids perturbing the delicate device codegen.
macro(sushiruntime_gpu_finalize)
    if(SR_SYCL_TOOLCHAIN STREQUAL "adaptivecpp")
        # AdaptiveCpp selects backends through ACPP_TARGETS. Respect an explicit
        # caller/preset value; otherwise publish what the backends declared.
        if(NOT DEFINED ACPP_TARGETS OR ACPP_TARGETS STREQUAL "")
            string(REPLACE ";" ";" _sr_acpp "${SR_GPU_ACPP_TARGETS}")
            set(ACPP_TARGETS "${_sr_acpp}" CACHE STRING "AdaptiveCpp targets" FORCE)
        endif()
        message(STATUS "GPU backend: AdaptiveCpp (ACPP_TARGETS=${ACPP_TARGETS})")

    elseif(SR_SYCL_TOOLCHAIN STREQUAL "intel-llvm" OR SR_SYCL_TOOLCHAIN STREQUAL "oneapi")
        if(SR_GPU_OFFLOAD_ADDED)
            string(REPLACE ";" "," _sr_targets "${SR_GPU_CLANG_TRIPLES}")
            add_compile_options(-fsycl-targets=${_sr_targets} ${SR_GPU_CLANG_OPTIONS})
            add_link_options(-fsycl-targets=${_sr_targets} ${SR_GPU_CLANG_OPTIONS})
            message(STATUS "GPU backend: SYCL targets = ${_sr_targets}")
        else()
            # Only spir64 => JIT default; emit nothing (CPU/Intel-GPU/Windows path).
            message(STATUS "GPU backend: CPU/OpenCL (spir64 JIT); no offload triple.")
        endif()

    else()
        message(STATUS "GPU backend: none for toolchain '${SR_SYCL_TOOLCHAIN}'.")
    endif()
endmacro()
