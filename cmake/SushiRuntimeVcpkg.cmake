# Vcpkg.cmake — vcpkg root discovery (Windows only).
#
# Single responsibility: locate a vcpkg tree and point CMAKE_TOOLCHAIN_FILE at
# it, without hardcoding any drive path. Must be included BEFORE project() so the
# toolchain file is in effect for the compiler probe.
#
# Discovery order (first hit wins, and a caller-supplied value is never
# overwritten):
#   1. VCPKG_ROOT env var
#   2. the shared SushiStack workspace tree ($SUSHISTACK_HOME/dependencies/vcpkg)
#   3. C:/vcpkg — only if it actually exists

if(NOT WIN32)
    return()
endif()

if((NOT DEFINED VCPKG_ROOT OR VCPKG_ROOT STREQUAL "") AND DEFINED ENV{VCPKG_ROOT})
    set(VCPKG_ROOT "$ENV{VCPKG_ROOT}" CACHE PATH "Path to vcpkg root")
elseif((NOT DEFINED VCPKG_ROOT OR VCPKG_ROOT STREQUAL "") AND DEFINED ENV{SUSHISTACK_HOME}
       AND EXISTS "$ENV{SUSHISTACK_HOME}/dependencies/vcpkg")
    set(VCPKG_ROOT "$ENV{SUSHISTACK_HOME}/dependencies/vcpkg" CACHE PATH "Path to vcpkg root (workspace)")
elseif((NOT DEFINED VCPKG_ROOT OR VCPKG_ROOT STREQUAL "") AND EXISTS "C:/vcpkg")
    set(VCPKG_ROOT "C:/vcpkg" CACHE PATH "Path to vcpkg root (fallback)")
endif()

if(VCPKG_ROOT AND (NOT DEFINED CMAKE_TOOLCHAIN_FILE OR CMAKE_TOOLCHAIN_FILE STREQUAL ""))
    set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE FILEPATH "vcpkg toolchain file")
endif()

if(VCPKG_ROOT)
    message(STATUS ">>> Using VCPKG_ROOT: ${VCPKG_ROOT}")
endif()
