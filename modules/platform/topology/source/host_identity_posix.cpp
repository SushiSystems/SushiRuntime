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
