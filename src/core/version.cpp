/****************************************************************/
/* version.cpp                                                  */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file version.cpp
 * @brief Returns the version the library was compiled with.
 * @author Mustafa Garip
 */

#include <SushiRuntime/version.hpp>

namespace SushiRuntime
{
    // The macros are frozen at library compile time (src/core/README.md).
    Version runtime_version() noexcept
    {
        return Version{SUSHIRUNTIME_VERSION_MAJOR,
                       SUSHIRUNTIME_VERSION_MINOR,
                       SUSHIRUNTIME_VERSION_PATCH};
    }

    const char* runtime_version_string() noexcept
    {
        return SUSHIRUNTIME_VERSION_STRING;
    }

} // namespace SushiRuntime
