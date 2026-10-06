# GpuBackend.cmake
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

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
#   SR_GPU_CLANG_OPTIONS  extra driver flags: -Xsycl-target-backend ... and toolkit paths.
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

# --- Toolkit search paths ------------------------------------------------------

# sushiruntime_gpu_versioned_prefixes(<out_var> <glob>...)
#   Expand install-prefix globs into real directories, newest first.
#
# find_program's PATHS does NOT glob -- a pattern like /usr/local/cuda-*/bin is
# taken as a literal directory name and simply never matches. That was a live
# defect: the vendor toolkits do not put themselves on PATH, so those versioned
# prefixes were the whole reason the search was written, and passing them raw
# meant a machine whose only CUDA lived at /usr/local/cuda-12.6 (an install with
# no /usr/local/cuda symlink, or several versions side by side) reported "no
# accelerator toolkit found" and quietly built CPU-only -- exactly the outcome
# those PATHS entries were added to prevent.
#
# Newest first, because find_program takes the first match and a machine with
# several toolkits installed wants the newest one. NATURAL ordering is what makes
# that true: a plain sort compares text, which puts cuda-9.0 above cuda-12.6.
function(sushiruntime_gpu_versioned_prefixes out_var)
    file(GLOB expanded ${ARGN})
    list(SORT expanded COMPARE NATURAL)
    list(REVERSE expanded)
    set(${out_var} "${expanded}" PARENT_SCOPE)
endfunction()

# --- Arch resolution: the shape the accelerator backends share -----------------
#
# CUDA and ROCm answer the same question — "which architectures am I building
# for?" — by the same steps: probe the hardware, collapse duplicates, dispatch on
# how many distinct answers came back, publish the result, reject anything
# malformed. Only the probe differs (nvidia-smi vs rocm_agent_enumerator, two
# unrelated output formats), so the probe stays in the vendor file and every step
# downstream of it lives here, parameterised by vocabulary rather than by vendor.
# This file still knows no vendor: it is handed the words it prints.
#
# Macros, not functions, and positional parameters, not cmake_parse_arguments:
#
#   * The no-GPU path must `return()` out of the *vendor file* so DetectGpu.cmake
#     regains control and reads SR_GPU_BACKEND_DECLINED. Only a macro's paste
#     semantics let a return() reach that far; from a function it would return
#     from the function alone. The resolved arch must land in the caller's scope
#     for the same reason.
#   * A macro's ${ARGN} is re-expanded, which splits any argument containing a
#     semicolon — and these take docstrings that show CMake list syntax ("61;86").
#     Named positional parameters bind per-argument and keep such a string whole.
#
# Both take the *names* of the variables they read and write, so a list crosses
# the boundary as one variable rather than as flattened arguments.

# Turn the probe's findings into a resolved architecture, or decline/fail when
# there were none.
#
#   _var             name of the arch variable to write (SR_CUDA_ARCH)
#   _candidates_var  name of the list the vendor's probe filled, deduplicated
#   _backend         SR_GPU_BACKEND value that selects this vendor (cuda)
#   _probe           the probe's program name, for the prose (nvidia-smi)
#   _noun            what one entry is called (compute capability)
#   _examples        a concrete flag with a few real values, quoted into both
#                    the decline warning and the hard failure
macro(sushiruntime_gpu_resolve_arch _var _candidates_var _backend _probe _noun _examples)
    list(LENGTH ${_candidates_var} _sr_ra_count)

    if(_sr_ra_count EQUAL 1)
        set(${_var} "${${_candidates_var}}")
        message(STATUS "GPU auto-detect: ${_probe} reports one ${_noun} "
                       "-> ${_var}=${${_var}}")

    elseif(_sr_ra_count EQUAL 0)
        # How loudly this fails depends on who asked for the backend. Auto-detect
        # selected it by finding a toolkit, which says a compiler is installed,
        # not that a card is present -- the usual shape of a CI or artifact build
        # box. Killing the build there would make "auto" a mode that fails on a
        # machine it was supposed to figure out, so it declines and
        # DetectGpu.cmake demotes to the CPU strategy. An explicit
        # -DSR_GPU_BACKEND=<vendor> is a stated intent, and stating it without an
        # architecture is the omission this whole mechanism exists to refuse.
        if(NOT SR_GPU_BACKEND_EXPLICIT)
            message(WARNING
                "${_backend} was auto-selected because its toolkit is installed, "
                "but ${_probe} reports no ${_noun}, so ${_var} cannot be resolved. "
                "Falling back to the CPU/OpenCL path."
                " To build ${_backend} offload on a machine with no such GPU, "
                "state both explicitly: -DSR_GPU_BACKEND=${_backend} together "
                "with e.g. ${_examples}")
            set(SR_GPU_BACKEND_DECLINED TRUE)
            return()
        endif()
        message(FATAL_ERROR
            "${_var} could not be resolved: ${_probe} is unavailable or reported "
            "no ${_noun}, and there is no default to fall back on.\n"
            "Pass the target explicitly, e.g. ${_examples}. A build machine with "
            "no such GPU must always state its target this way.")

    else()
        # Several different cards. Target all of them rather than electing one:
        # the set is the honest reading of what is installed, and electing
        # whichever the probe listed first would be exactly the guess this
        # mechanism refuses. Announced loudly, because the build just got longer
        # by a factor nobody typed.
        string(REPLACE ";" ", " _sr_ra_list "${${_candidates_var}}")
        set(${_var} "${${_candidates_var}}")
        message(STATUS "GPU auto-detect: ${_probe} reports more than one ${_noun} "
                       "(${_sr_ra_list}); building for all of them. Narrow with "
                       "-D${_var}=<value> to cut build time and binary size.")
    endif()
endmacro()

# Publish the arch as a cache entry and reject anything that is not one.
#
# Runs unconditionally, unlike the resolution above: an arch the user stated on
# the command line skips the probe entirely but must still be cached and checked.
#
#   _var      name of the arch variable (SR_CUDA_ARCH)
#   _noun     what a valid entry is, phrased to follow "which is not "
#   _pattern  regex every entry must match
#   _hint     one sentence showing the accepted spelling
#   _doc      the cache docstring
#
# _doc arrives as a single parameter, which is the point: set()'s cache signature
# takes exactly one docstring, and adjacent quoted strings do not concatenate in
# CMake. Split across several, the call silently stops matching that signature and
# degrades into a plain set() whose value becomes the arch *plus* the words CACHE,
# STRING and the doc text — which the loop below then reports as a malformed arch.
#
# FORCE, because the value being published is the answer this configure arrived at
# and the cache may already disagree. Without it, an existing entry wins and the
# normal variable holding the resolved arch is dropped -- which is what an empty
# -DSR_CUDA_ARCH= did: the probe ran, announced "SR_CUDA_ARCH=89", and then lost
# it to the empty cache entry, leaving an sm_ triple with no architecture in it and
# no offload target added at all. The STATUS line said 89 while the build carried
# nothing. Forcing cannot overwrite a stated arch with something else, because a
# stated arch is what skipped the probe: the value written back is the same one.
macro(sushiruntime_gpu_publish_arch _var _noun _pattern _hint _doc)
    set(${_var} "${${_var}}" CACHE STRING "${_doc}" FORCE)

    foreach(_sr_pa_arch IN LISTS ${_var})
        if(NOT _sr_pa_arch MATCHES "${_pattern}")
            message(FATAL_ERROR
                "${_var} contains '${_sr_pa_arch}', which is not ${_noun}. ${_hint}")
        endif()
    endforeach()
endmacro()

# --- Finalize: render accumulators into flags ----------------------------------
#
# A macro so the directory-scope add_compile_options/add_link_options land in the
# caller's (root) scope and cascade to add_subdirectory() exactly like the former
# global flags. The runtime's own sources take the device flags at directory scope; the
# same flags are published on the sushiruntime_gpu_offload INTERFACE target
# (SushiRuntime::gpu_offload) for consumers outside this directory.
macro(sushiruntime_gpu_finalize)
    # sushiruntime_gpu_offload carries the device targets to consumers outside this directory.
    if(NOT TARGET sushiruntime_gpu_offload)
        add_library(sushiruntime_gpu_offload INTERFACE)
        add_library(SushiRuntime::gpu_offload ALIAS sushiruntime_gpu_offload)
    endif()

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
            target_compile_options(sushiruntime_gpu_offload INTERFACE
                -fsycl-targets=${_sr_targets} ${SR_GPU_CLANG_OPTIONS})
            target_link_options(sushiruntime_gpu_offload INTERFACE
                -fsycl-targets=${_sr_targets} ${SR_GPU_CLANG_OPTIONS})
            message(STATUS "GPU backend: SYCL targets = ${_sr_targets}")
        else()
            # Only spir64 => JIT default; emit nothing (CPU/Intel-GPU/Windows path).
            message(STATUS "GPU backend: CPU/OpenCL (spir64 JIT); no offload triple.")
        endif()

    else()
        message(STATUS "GPU backend: none for toolchain '${SR_SYCL_TOOLCHAIN}'.")
    endif()
endmacro()
