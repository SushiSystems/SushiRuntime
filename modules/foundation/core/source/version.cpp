/**************************************************************************/
/* version.cpp                                                            */
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
 * @file version.cpp
 * @brief Returns the version the library was compiled with.
 * @author Mustafa Garip
 */

#include <SushiRuntime/core/version.hpp>

namespace SushiRuntime
{
    // The macros are frozen at library compile time (modules/foundation/core/README.md).
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
