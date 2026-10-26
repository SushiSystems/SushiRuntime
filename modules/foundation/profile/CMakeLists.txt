# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

# The runtime's front over SushiProfiler. SushiProfiler is linked only when the runtime reports to
# it; with the option off the macros expand to nothing and no package is needed.
set(profile_external "")
if(SR_ENABLE_PROFILER)
    set(profile_external SushiProfiler::SushiProfiler)
endif()

sushiruntime_add_module(
    NAME profile TIER foundation INTERNAL
    HEADER_ONLY
    PUBLIC_EXTERNAL ${profile_external})

# SR_ENABLE_PROFILER and SR_PROFILE_LEVEL reach the code through this header and no compile
# definition, so a change of the level rebuilds the files that include it and no others.
set(profile_generated "${CMAKE_CURRENT_BINARY_DIR}/include")
configure_file(profile_config.hpp.in
    "${profile_generated}/SushiRuntime/profile/profile_config.hpp" @ONLY)
target_include_directories(sushiruntime_module_profile INTERFACE
    "$<BUILD_INTERFACE:${profile_generated}>")
