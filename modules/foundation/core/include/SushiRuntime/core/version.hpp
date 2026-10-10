/**************************************************************************/
/* version.hpp                                                            */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
/**************************************************************************/

/**
 * @file version.hpp
 * @brief Declares the library's version macros, runtime_version() and the version check.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/README.md
 */

#pragma once

#include <string>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/export.hpp>

/** @brief Major version of the headers being compiled against. */
#define SUSHIRUNTIME_VERSION_MAJOR 0
/** @brief Minor version of the headers being compiled against. */
#define SUSHIRUNTIME_VERSION_MINOR 3
/** @brief Patch version of the headers being compiled against. */
#define SUSHIRUNTIME_VERSION_PATCH 0

/** @brief Dotted version of the headers being compiled against, as a literal. */
#define SUSHIRUNTIME_VERSION_STRING "0.3.0"

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
     * @brief Returns the version the loaded library was built from.
     * @see modules/surface/api/include/SushiRuntime/README.md
     */
    [[nodiscard]] SUSHIRUNTIME_API Version runtime_version() noexcept;

    /**
     * @brief The version the loaded library was built from, dotted.
     * @return A null-terminated literal owned by the library; never null.
     */
    [[nodiscard]] SUSHIRUNTIME_API const char* runtime_version_string() noexcept;

    /**
     * @brief Returns whether the loaded library matches these headers, by major and minor.
     * @see modules/surface/api/include/SushiRuntime/README.md
     */
    [[nodiscard]] inline bool version_matches() noexcept
    {
        const Version loaded = runtime_version();
        return loaded.major == SUSHIRUNTIME_VERSION_MAJOR &&
               loaded.minor == SUSHIRUNTIME_VERSION_MINOR;
    }

    namespace Detail
    {
        /** @brief Returns @p version as "major.minor.patch". */
        [[nodiscard]] inline std::string dotted(const Version& version)
        {
            return std::to_string(version.major) + "." + std::to_string(version.minor) + "." +
                   std::to_string(version.patch);
        }

        /**
         * @brief Throws unless two versions agree by major and minor.
         * @param headers The version the caller's headers declare.
         * @param loaded  The version the loaded library reports.
         * @throws Core::SushiException With Errc::unsupported, naming both versions.
         */
        inline void require_same_release(const Version& headers, const Version& loaded)
        {
            if (headers.major == loaded.major && headers.minor == loaded.minor)
                return;

            const bool library_is_older =
                loaded.major < headers.major ||
                (loaded.major == headers.major && loaded.minor < headers.minor);
            throw Core::SushiException(Core::Error{
                Core::Errc::unsupported,
                "SushiRuntime headers are " + dotted(headers) + " and the loaded library is " +
                    dotted(loaded) + "; " + (library_is_older ? "an older" : "a newer") +
                    " sushiruntime shared library is earlier on the loader path."});
        }
    } // namespace Detail

    /**
     * @brief Throws unless the loaded library matches these headers, by major and minor.
     * @throws Core::SushiException With Errc::unsupported, naming both versions.
     * @see modules/surface/api/include/SushiRuntime/README.md
     */
    inline void require_matching_version()
    {
        Detail::require_same_release(Version{SUSHIRUNTIME_VERSION_MAJOR,
                                             SUSHIRUNTIME_VERSION_MINOR,
                                             SUSHIRUNTIME_VERSION_PATCH},
                                     runtime_version());
    }

} // namespace SushiRuntime
