# CheckTestSizes.cmake
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

# Run with cmake -P by the build step cmake/SushiRuntimeTests.cmake adds to each test program.
# Fails when a suite in SOURCES has no size prefix, since such a suite would register no test,
# and writes STAMP when every suite has one.
#
#   cmake -DSOURCES=<file|file...> -DSTAMP=<file> -P CheckTestSizes.cmake

string(REPLACE "|" ";" sources "${SOURCES}")
foreach(source IN LISTS sources)
    file(READ "${source}" text)
    string(REGEX MATCHALL "(TEST|TEST_F|TEST_P|TYPED_TEST|TYPED_TEST_P)\\([ \t\r\n]*[A-Za-z0-9_]+"
        declarations "${text}")
    foreach(declaration IN LISTS declarations)
        string(REGEX REPLACE "^[A-Z_]+\\([ \t\r\n]*" "" suite "${declaration}")
        if(NOT suite MATCHES "^(Small|Medium|Large)_")
            message(FATAL_ERROR
                "${source}: the suite ${suite} has no size prefix (Small_, Medium_ or Large_); "
                "tests/README.md gives the sizes.")
        endif()
    endforeach()
endforeach()
file(WRITE "${STAMP}" "")
