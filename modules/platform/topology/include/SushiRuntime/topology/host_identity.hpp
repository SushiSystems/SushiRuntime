/**************************************************************************/
/* host_identity.hpp                                                      */
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
 * @file host_identity.hpp
 * @brief Declares the queries that name the machine the library runs on.
 * @author Mustafa Garip
 * @see modules/platform/topology/README.md
 */

#pragma once

#include <string>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/machine/operating_system.hpp>

namespace SushiRuntime
{
    namespace Internal::Topology
    {
        /**
         * @brief Returns the host name of this machine.
         * @return The name, or an empty string when the platform does not give one.
         */
        [[nodiscard]] SUSHIRUNTIME_API std::string host_name();

        /**
         * @brief Returns the operating system the library runs on.
         * @return Unknown for a system the library does not name.
         */
        [[nodiscard]] SUSHIRUNTIME_API OperatingSystem host_operating_system() noexcept;
    } // namespace Internal::Topology
} // namespace SushiRuntime
