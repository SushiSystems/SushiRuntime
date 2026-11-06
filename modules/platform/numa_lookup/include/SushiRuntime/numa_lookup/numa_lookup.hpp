/**************************************************************************/
/* numa_lookup.hpp                                                        */
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
 * @file numa_lookup.hpp
 * @brief Declares local_numa_os_index, the lookup of the NUMA node local to a hwloc object.
 * @author Mustafa Garip
 * @see modules/platform/numa_lookup/README.md
 */

#pragma once

#include <optional>

#include <hwloc.h>

#include <SushiRuntime/core/export.hpp>

namespace SushiRuntime
{
    namespace Internal::Topology
    {
        /**
         * @brief Returns the OS index of the NUMA node local to a hwloc object.
         * @param topology A loaded hwloc topology; must not be null.
         * @param object An object of that topology, or null.
         * @return The OS index, or empty when hwloc reports no node for the object.
         * @see modules/platform/numa_lookup/README.md
         */
        [[nodiscard]] SUSHIRUNTIME_API std::optional<unsigned>
        local_numa_os_index(hwloc_topology_t topology, hwloc_obj_t object);
    } // namespace Topology
} // namespace SushiRuntime
