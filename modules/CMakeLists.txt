# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

# Adds every enabled module in the order of cmake/SushiRuntimeTiers.cmake, then composes
# the shared library from them. A new module is its folder and one line in that table.
sushiruntime_enabled_module_folders(module_folders)
foreach(module_folder IN LISTS module_folders)
    add_subdirectory("${module_folder}")
endforeach()
sushiruntime_compose_library()
