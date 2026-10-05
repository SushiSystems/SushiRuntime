/****************************************************************/
/* version.cpp                                                  */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

#include <SushiRuntime/version.hpp>

namespace SushiRuntime
{
    // These read the same macros the consumer sees, but they are frozen here at
    // the moment the LIBRARY was compiled. That is the whole point: a consumer
    // comparing its own macros against these is comparing two builds, and they
    // only agree if the binary it loaded is the one its headers describe.

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
