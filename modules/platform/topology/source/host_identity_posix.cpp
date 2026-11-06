/**************************************************************************/
/* host_identity_posix.cpp                                                */
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
 * @file host_identity_posix.cpp
 * @brief Implements the host identity queries on POSIX systems.
 * @author Mustafa Garip
 * @see modules/platform/topology/README.md
 */

#include <SushiRuntime/topology/host_identity.hpp>

#if !defined(_WIN32)

#include <unistd.h>

namespace SushiRuntime
{
    namespace Internal::Topology
    {
        std::string host_name()
        {
            char buffer[256] = {};
            if (gethostname(buffer, sizeof(buffer) - 1) != 0)
                return std::string();
            return std::string(buffer);
        }

        OperatingSystem host_operating_system() noexcept
        {
#if defined(__linux__)
            return OperatingSystem::Linux;
#else
            return OperatingSystem::Unknown;
#endif
        }
    } // namespace Topology
} // namespace SushiRuntime

#endif // !defined(_WIN32)
