/**************************************************************************/
/* environment.hpp                                                        */
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
 * @file environment.hpp
 * @brief Declares read_environment(), the one place the module reads an environment variable.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <optional>
#include <string>

#include <SushiRuntime/core/export.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Returns the value of an environment variable of this process.
         *
         * Reads through the platform's bounds-checked call where the plain one is
         * deprecated. Any thread may call; nothing in the module writes the environment.
         * @param name Not null; the variable's name, without `=`.
         * @return Nothing when the variable is unset; an empty string when it is set to none.
         */
        [[nodiscard]] SUSHIRUNTIME_API std::optional<std::string> read_environment(
            const char* name);

    } // namespace Distributed
} // namespace SushiRuntime
