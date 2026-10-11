# SushiRuntimeInstall.cmake
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

# Installs and exports the library, its headers and its package files.

include("${CMAKE_CURRENT_LIST_DIR}/SushiRuntimeRedistributables.cmake")

# Install layout and the package's own location. GNUInstallDirs is what makes an
# install respect a distribution's conventions (lib64, multiarch) instead of
# hardcoding "lib"; SR_INSTALL_CMAKEDIR is where find_package(SushiRuntime) looks.
include(GNUInstallDirs)
set(SR_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/SushiRuntime"
    CACHE STRING "Where the SushiRuntime CMake package files are installed")

# Installs the `sushiruntime` target, the include roots of the public modules, the export
# set and the package configuration files. Call it after sushiruntime_compose_library().
function(sushiruntime_install_package)
    # A module declared INTERNAL is absent from this list, so its headers are not installed.
    get_property(include_roots GLOBAL PROPERTY SUSHIRUNTIME_PUBLIC_INCLUDE_ROOTS)

    # The export set carries the library and the floating-point contract it links
    # PUBLIC. CMake refuses to export a target whose interface names a target no export
    # set contains, so sushiruntime_deterministic_fp is in the set.
    install(TARGETS sushiruntime sushiruntime_deterministic_fp
        EXPORT SushiRuntimeTargets
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

    foreach(include_root IN LISTS include_roots)
        install(DIRECTORY "${include_root}/"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
            FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h")
    endforeach()

    install(EXPORT SushiRuntimeTargets
        FILE SushiRuntimeTargets.cmake
        NAMESPACE SushiRuntime::
        DESTINATION "${SR_INSTALL_CMAKEDIR}")

    # The same targets, usable from a build tree without installing.
    export(EXPORT SushiRuntimeTargets
        FILE "${PROJECT_BINARY_DIR}/SushiRuntimeTargets.cmake"
        NAMESPACE SushiRuntime::)

    include(CMakePackageConfigHelpers)

    configure_package_config_file(
        "${SR_CMAKE_DIR}/SushiRuntimeConfig.cmake.in"
        "${PROJECT_BINARY_DIR}/SushiRuntimeConfig.cmake"
        INSTALL_DESTINATION "${SR_INSTALL_CMAKEDIR}"
    )

    # SameMinorVersion: the project is pre-1.0 and its API is not frozen, so a
    # consumer asking for 0.2 must not be handed 0.3. Revisit at the v1.0.0 freeze,
    # where SameMajorVersion becomes the honest compatibility statement.
    write_basic_package_version_file(
        "${PROJECT_BINARY_DIR}/SushiRuntimeConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMinorVersion
    )

    install(FILES
        "${PROJECT_BINARY_DIR}/SushiRuntimeConfig.cmake"
        "${PROJECT_BINARY_DIR}/SushiRuntimeConfigVersion.cmake"
        DESTINATION "${SR_INSTALL_CMAKEDIR}"
    )

    # The third-party libraries an installed consumer loads at run time. Called after
    # the export rules so the package is described before its dependencies are added
    # to it.
    sushiruntime_install_runtime_dependencies()
endfunction()
