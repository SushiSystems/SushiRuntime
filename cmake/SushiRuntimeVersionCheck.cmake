# SushiRuntimeVersionCheck.cmake
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

# Stops the configure when core/version.hpp and project() state different versions.
#
# The version literals in core/version.hpp are hand-written, so they are greppable and
# need no generated header on the include path. That is only safe if they cannot disagree
# with the version the build and the package advertise, so a disagreement stops the
# configure here.
set(SR_VERSION_HEADER
    "${PROJECT_SOURCE_DIR}/modules/foundation/core/include/SushiRuntime/core/version.hpp")
file(STRINGS "${SR_VERSION_HEADER}" SR_VERSION_STRING_LINE
     REGEX "^#define SUSHIRUNTIME_VERSION_STRING ")
string(REGEX MATCH "\"([0-9]+\\.[0-9]+\\.[0-9]+)\"" _sr_unused "${SR_VERSION_STRING_LINE}")
if(NOT CMAKE_MATCH_1 STREQUAL PROJECT_VERSION)
    message(FATAL_ERROR
        "Version mismatch: project() says ${PROJECT_VERSION}, "
        "${SR_VERSION_HEADER} says ${CMAKE_MATCH_1}. "
        "Update both; they are the same fact stated twice.")
endif()
