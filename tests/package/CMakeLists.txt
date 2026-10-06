# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

cmake_minimum_required(VERSION 3.20)

# An out-of-tree consumer of an INSTALLED SushiRuntime.
#
# This project is deliberately not part of the main build. It configures against
# the package the way a third party would — find_package, nothing else, no access
# to the runtime's source tree — because that is the only way to test the thing
# that actually breaks: a package that works only when the source tree happens to
# be next to it is not a package.
#
# Build it against an install prefix:
#
#   sr build
#   cmake --install build --prefix /tmp/sr-prefix
#   cmake -S tests/package -B /tmp/sr-consumer \
#         -DCMAKE_PREFIX_PATH=/tmp/sr-prefix \
#         -DCMAKE_CXX_COMPILER=<the same SYCL compiler>
#   cmake --build /tmp/sr-consumer
#   /tmp/sr-consumer/sushiruntime_package_consumer
#
# The compiler must be the SYCL one: the runtime's kernels are header templates
# instantiated in THIS project's translation units. See docs/INTEGRATION.md.

project(SushiRuntimePackageConsumer LANGUAGES CXX)

find_package(SushiRuntime REQUIRED)

# --- Flag hygiene ------------------------------------------------------------
#
# The assertion BB-3's split exists to make: a consumer inherits the library's
# floating-point contract and nothing else. Warnings, -Werror, sanitizers and the
# ISA of the machine that built the package are development choices of that
# project, and a consumer silently compiling under them gets build failures whose
# cause is invisible from its own source tree.
#
# Checked here rather than in the runtime's own tests because "what a consumer
# inherits" is only observable from the consumer side — in-tree, every target
# links the policy targets directly and the leak would be invisible.
get_target_property(_sr_interface_options
    SushiRuntime::SushiRuntime INTERFACE_COMPILE_OPTIONS)
if(NOT _sr_interface_options)
    set(_sr_interface_options "")
endif()

get_target_property(_sr_interface_links
    SushiRuntime::SushiRuntime INTERFACE_LINK_LIBRARIES)
if(NOT _sr_interface_links)
    set(_sr_interface_links "")
endif()

set(_sr_forbidden_flags "-Werror" "-Wall" "-Wextra" "-pedantic"
                        "/W4" "/permissive-"
                        "-march=" "/arch:" "-fsanitize" "-ftls-model")

foreach(_sr_flag IN LISTS _sr_forbidden_flags)
    string(FIND "${_sr_interface_options}" "${_sr_flag}" _sr_found)
    if(NOT _sr_found EQUAL -1)
        message(FATAL_ERROR
            "SushiRuntime's exported interface leaks '${_sr_flag}' to consumers. "
            "Development flags belong on the PRIVATE policy targets; see "
            "cmake/BuildPolicy.cmake. Full interface: ${_sr_interface_options}")
    endif()
endforeach()

foreach(_sr_leaked IN ITEMS sushiruntime_warnings sushiruntime_perf sushiruntime_sanitizers)
    string(FIND "${_sr_interface_links}" "${_sr_leaked}" _sr_found)
    if(NOT _sr_found EQUAL -1)
        message(FATAL_ERROR
            "SushiRuntime's exported interface names the in-tree policy target "
            "'${_sr_leaked}'. It should be wrapped in BUILD_INTERFACE. "
            "Full link interface: ${_sr_interface_links}")
    endif()
endforeach()

message(STATUS "SushiRuntime interface compile options: ${_sr_interface_options}")
message(STATUS "SushiRuntime distributed layer: ${SushiRuntime_DISTRIBUTED}")
message(STATUS "SushiRuntime deterministic FP:  ${SushiRuntime_DETERMINISTIC_FP}")

# --- The consumer ------------------------------------------------------------

add_executable(sushiruntime_package_consumer main.cpp)

target_link_libraries(sushiruntime_package_consumer PRIVATE
    SushiRuntime::SushiRuntime
)

set_target_properties(sushiruntime_package_consumer PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
