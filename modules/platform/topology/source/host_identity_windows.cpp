/**************************************************************************/
/* host_identity_windows.cpp                                              */
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
 * @file host_identity_windows.cpp
 * @brief Implements the host identity queries on Windows.
 * @author Mustafa Garip
 * @see modules/platform/topology/README.md
 */

#include <SushiRuntime/topology/host_identity.hpp>

#if defined(_WIN32)

#if !defined(NOMINMAX)
    #define NOMINMAX
#endif
#if !defined(WIN32_LEAN_AND_MEAN)
    #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace SushiRuntime
{
    namespace Internal::Topology
    {
        std::string host_name()
        {
            DWORD size = 0;
            // The sizing call fails with ERROR_MORE_DATA and sets size to the length with the null.
            GetComputerNameExA(ComputerNameDnsHostname, nullptr, &size);
            if (size == 0)
                return std::string();
            std::string name(size, '\0');
            if (GetComputerNameExA(ComputerNameDnsHostname, name.data(), &size) == 0)
                return std::string();
            name.resize(size);
            return name;
        }

        OperatingSystem host_operating_system() noexcept
        {
            return OperatingSystem::Windows;
        }
    } // namespace Topology
} // namespace SushiRuntime

#endif // defined(_WIN32)
