# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

cmake_minimum_required(VERSION 3.22) # gtest_discover_tests(TEST_FILTER ...)

# The tree mirrors modules/: the tests of a module sit in tests/<tier>/<module>/, whose
# CMakeLists.txt builds sr_<module>_tests and, under SR_BUILD_BENCHMARKS,
# sr_<module>_benchmarks. A folder is added only when its module is switched on.
# cmake/SushiRuntimeTests.cmake holds the function every program is built with.
#
#   Package.*   package/, through tools/packaging

sushiruntime_enabled_module_folders(module_folders)
foreach(module_folder IN LISTS module_folders)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${module_folder}/CMakeLists.txt")
        add_subdirectory("${module_folder}")
    endif()
endforeach()

# Whether an installed SushiRuntime is usable from outside its own tree. Its own label,
# because it installs, configures a second CMake project, compiles it and runs it.
#
# Two tests joined by a fixture, so a failure says which half broke: a package that will
# not install is a different defect from one that installs and will not run.
find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_FOUND)
    set(SR_PACKAGE_STAGE "${PROJECT_BINARY_DIR}/stage")

    add_test(NAME Package.InstallsToAPrefix
        COMMAND "${CMAKE_COMMAND}" --install "${PROJECT_BINARY_DIR}"
                                   --prefix "${SR_PACKAGE_STAGE}")
    set_tests_properties(Package.InstallsToAPrefix PROPERTIES
        FIXTURES_SETUP SR_PACKAGE
        LABELS "package")

    # The compiler is passed through: the runtime's kernels are header templates
    # instantiated in the consumer's own translation units, so a consumer built with a
    # different SYCL compiler is not testing this package.
    add_test(NAME Package.RunsAConsumerWithNothingButThePrefix
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tools/packaging/verify_installed_package.py"
                "${SR_PACKAGE_STAGE}"
                --compiler "${CMAKE_CXX_COMPILER}"
                --cmake "${CMAKE_COMMAND}"
                --ninja "${CMAKE_MAKE_PROGRAM}")
    set_tests_properties(Package.RunsAConsumerWithNothingButThePrefix PROPERTIES
        FIXTURES_REQUIRED SR_PACKAGE
        LABELS "package")
else()
    message(STATUS "Python3 not found: the packaging suite will not be registered.")
endif()
