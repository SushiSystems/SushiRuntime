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
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
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
