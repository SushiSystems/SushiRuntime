/**************************************************************************/
/* version.hpp                                                            */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

#pragma once

/**
 * @file version.hpp
 * @brief The library's version, in both of the forms a consumer needs.
 *
 * The macros are the version of the HEADERS being compiled against;
 * runtime_version() reports the version the loaded library was BUILT from.
 * Those two are different questions, and a consumer that pins by tag needs both
 * to catch the case they disagree — a stale shared library found first on the
 * loader path is otherwise a silent, arbitrarily-late failure rather than a
 * startup check. See version_matches().
 *
 * The literals here are checked against the project's CMake version at configure
 * time, so this header and the build cannot drift apart.
 */

#include <SushiRuntime/core/export.hpp>

/** @brief Major version of the headers being compiled against. */
#define SUSHIRUNTIME_VERSION_MAJOR 0
/** @brief Minor version of the headers being compiled against. */
#define SUSHIRUNTIME_VERSION_MINOR 1
/** @brief Patch version of the headers being compiled against. */
#define SUSHIRUNTIME_VERSION_PATCH 0

/** @brief Dotted version of the headers being compiled against, as a literal. */
#define SUSHIRUNTIME_VERSION_STRING "0.1.0"

/**
 * @brief The header version as one comparable integer.
 *
 * Encoded so ordinary integer comparison orders releases:
 * `#if SUSHIRUNTIME_VERSION >= SUSHIRUNTIME_VERSION_ENCODE(0, 2, 0)`.
 */
#define SUSHIRUNTIME_VERSION_ENCODE(major, minor, patch) \
    ((major) * 10000 + (minor) * 100 + (patch))

/** @brief The header version, encoded by SUSHIRUNTIME_VERSION_ENCODE. */
#define SUSHIRUNTIME_VERSION                                     \
    SUSHIRUNTIME_VERSION_ENCODE(SUSHIRUNTIME_VERSION_MAJOR,      \
                                SUSHIRUNTIME_VERSION_MINOR,      \
                                SUSHIRUNTIME_VERSION_PATCH)

namespace SushiRuntime
{
    /**
     * @brief A library version, split into its three components.
     */
    struct Version
    {
        int major = 0;  /**< Incompatible API changes. */
        int minor = 0;  /**< Backward-compatible additions. */
        int patch = 0;  /**< Backward-compatible fixes. */
    };

    /**
     * @brief The version the loaded library was built from.
     *
     * Compiled into the library, not into the caller, so it answers "which
     * binary am I actually running against" rather than "which headers did I
     * compile against".
     *
     * @return The library's own version.
     */
    SUSHIRUNTIME_API Version runtime_version() noexcept;

    /**
     * @brief The version the loaded library was built from, dotted.
     * @return A null-terminated literal owned by the library; never null.
     */
    SUSHIRUNTIME_API const char* runtime_version_string() noexcept;

    /**
     * @brief Whether the loaded library matches the headers this call compiled against.
     *
     * The check a consumer should make once at startup. A mismatch means the
     * headers and the binary came from different releases — the classic cause is
     * an older shared library earlier on the loader path — and the failure it
     * otherwise produces is a crash or wrong answer at an arbitrary later point,
     * with nothing pointing back at the real cause.
     *
     * Compares major and minor only: a patch release is defined not to change
     * layout or behaviour, so refusing to run against one would reject a
     * legitimate upgrade.
     *
     * @return True when the loaded library is compatible with these headers.
     */
    inline bool version_matches() noexcept
    {
        const Version loaded = runtime_version();
        return loaded.major == SUSHIRUNTIME_VERSION_MAJOR &&
               loaded.minor == SUSHIRUNTIME_VERSION_MINOR;
    }

} // namespace SushiRuntime
