# Determinism.cmake
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

# Determinism.cmake — the floating-point contract as an exported target.
#
# Single responsibility: define an INTERFACE target `sushiruntime_deterministic_fp`
# that pins the floating-point semantics every translation unit compiles under.
#
# Why this is exported rather than in-tree policy: the runtime's kernels are
# header templates, so a consumer's own translation units instantiate them and
# compile the arithmetic themselves. The fixed-order reduction, the host-side
# folds it ends in, and every consumer kernel that runs a host pass only produce
# the same bits between two builds if both builds forbid the same
# transformations. Fast-math licenses reassociation; FMA contraction changes
# rounding by fusing a multiply and an add. Either one silently breaks the
# same-binary, same-device determinism the runtime sells, so the contract has to
# travel with the target rather than stop at this tree's edge.
#
# ON by default because deterministic simulation is the primary workload. A
# throughput consumer that does not need replay can configure with
# SR_DETERMINISTIC_FP=OFF and get its compiler's defaults back.

add_library(sushiruntime_deterministic_fp INTERFACE)

if(SR_DETERMINISTIC_FP)
    # Selected by compiler frontend rather than by platform: the same clang can
    # present a GNU or an MSVC command line depending on how it was invoked, and
    # the exported target has to be right for the consumer's compiler, not for
    # the one that configured this tree.
    #
    # icx/icx-cl default to -fp-model=fast, so naming the model is not redundant
    # there the way it would be on plain clang.
    # $<CXX_COMPILER_FRONTEND_VARIANT:...> is a CMake 3.30 generator expression and
    # this project's floor is 3.20, so it cannot be used unconditionally: on an
    # older CMake it is not an unknown-but-ignored genex, it is a hard
    # "Expression did not evaluate to a known generator expression" at generate
    # time, which is how a lane with an older CMake than the developer's fails.
    #
    # Where it is available it is worth keeping, because it evaluates in the
    # *consumer's* generate step: a consumer compiling with an MSVC-frontend
    # driver gets /fp:precise even when this tree was configured with a
    # GNU-frontend one. The fallback cannot do that — it bakes in the frontend of
    # whoever built the runtime — so it is a genuine loss of fidelity for
    # cross-frontend consumption, taken only when CMake leaves no choice.
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.30)
        target_compile_options(sushiruntime_deterministic_fp INTERFACE
            "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_FRONTEND_VARIANT:MSVC>>:/fp:precise>"
            "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_FRONTEND_VARIANT:GNU>>:-ffp-contract=off>"
            "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_FRONTEND_VARIANT:GNU>>:-fno-fast-math>"
            "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_FRONTEND_VARIANT:GNU>,$<CXX_COMPILER_ID:IntelLLVM>>:-fp-model=strict>"
        )
    elseif(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        target_compile_options(sushiruntime_deterministic_fp INTERFACE
            "$<$<COMPILE_LANGUAGE:CXX>:/fp:precise>")
    else()
        target_compile_options(sushiruntime_deterministic_fp INTERFACE
            "$<$<COMPILE_LANGUAGE:CXX>:-ffp-contract=off>"
            "$<$<COMPILE_LANGUAGE:CXX>:-fno-fast-math>")
        if(CMAKE_CXX_COMPILER_ID STREQUAL "IntelLLVM")
            # Strict, not precise: precise re-enables contraction on the clang driver.
            target_compile_options(sushiruntime_deterministic_fp INTERFACE
                "$<$<COMPILE_LANGUAGE:CXX>:-fp-model=strict>")
        endif()
    endif()

    # The MSVC-style command line above pins the model but says nothing about
    # contraction, and the two are separate axes on the compiler that matters
    # most here: icx-cl documents its contraction default as `fast` for every
    # fp-model except `strict`, so a Windows oneAPI build under /fp:precise can
    # still fuse a multiply and an add and round once instead of twice. That is
    # a different result from the same source on a Linux lane, which is exactly
    # what this target exists to prevent.
    #
    # Probed rather than assumed: the spelling that works is a property of the
    # driver, not of the frontend variant, and an option this tree guesses wrong
    # would be silently ignored (or fatal) in a consumer's build. cl.exe proper
    # needs nothing — it does not contract unless asked with /fp:contract.
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC" AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        include(CheckCXXCompilerFlag)
        check_cxx_compiler_flag("-ffp-contract=off" SR_HAVE_CLANG_STYLE_FP_CONTRACT)
        if(SR_HAVE_CLANG_STYLE_FP_CONTRACT)
            target_compile_options(sushiruntime_deterministic_fp INTERFACE
                "$<$<COMPILE_LANGUAGE:CXX>:-ffp-contract=off>")
        else()
            check_cxx_compiler_flag("/Qfp-contract=off" SR_HAVE_MSVC_STYLE_FP_CONTRACT)
            if(SR_HAVE_MSVC_STYLE_FP_CONTRACT)
                target_compile_options(sushiruntime_deterministic_fp INTERFACE
                    "$<$<COMPILE_LANGUAGE:CXX>:/Qfp-contract=off>")
            else()
                message(WARNING
                    "SR_DETERMINISTIC_FP is ON but this compiler accepts no known "
                    "contraction switch; FMA contraction stays at the compiler's "
                    "default and results may differ from a GNU-frontend build.")
            endif()
        endif()
    endif()

    target_compile_definitions(sushiruntime_deterministic_fp INTERFACE SR_DETERMINISTIC_FP=1)
else()
    message(STATUS "SR_DETERMINISTIC_FP=OFF — replay is not byte-equal across builds.")
endif()
