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
 * @brief Declares the library's version macros and runtime_version().
 * @author Mustafa Garip
 * @see include/SushiRuntime/README.md
 */

#pragma once

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
     * @see include/SushiRuntime/README.md
     */
    SUSHIRUNTIME_API Version runtime_version() noexcept;

    /**
     * @brief The version the loaded library was built from, dotted.
     * @return A null-terminated literal owned by the library; never null.
     */
    SUSHIRUNTIME_API const char* runtime_version_string() noexcept;

    /**
     * @brief Returns whether the loaded library matches these headers, by major and minor.
     * @see include/SushiRuntime/README.md
     */
    inline bool version_matches() noexcept
    {
        const Version loaded = runtime_version();
        return loaded.major == SUSHIRUNTIME_VERSION_MAJOR &&
               loaded.minor == SUSHIRUNTIME_VERSION_MINOR;
    }

} // namespace SushiRuntime
