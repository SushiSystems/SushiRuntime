# SushiRuntimePackaging.cmake
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

# Declares the CPack archive of the installed tree.
#
# An archive of the installed tree, for handing the SDK to a consumer who is not
# building it from source. The layout is whatever the install rules above produce,
# so the archive and a plain `cmake --install` cannot drift apart.
set(CPACK_PACKAGE_NAME "SushiRuntime")
set(CPACK_PACKAGE_VENDOR "SushiSystems")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "SushiRuntime-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}")
set(CPACK_PACKAGE_DIRECTORY "${PROJECT_BINARY_DIR}/packages")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
if(WIN32)
    set(CPACK_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "TGZ")
endif()
include(CPack)
