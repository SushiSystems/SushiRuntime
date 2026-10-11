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
 * @file numa_lookup.hpp
 * @brief Declares local_numa_os_index, the lookup of the NUMA node local to a hwloc object.
 * @author Mustafa Garip
 * @see modules/platform/topology/README.md
 */

#pragma once

#include <optional>

#include <hwloc.h>

#include <SushiRuntime/core/export.hpp>

namespace SushiRuntime
{
    namespace Topology
    {
        /**
         * @brief Returns the OS index of the NUMA node local to a hwloc object.
         * @param topology A loaded hwloc topology; must not be null.
         * @param object An object of that topology, or null.
         * @return The OS index, or empty when hwloc reports no node for the object.
         * @see modules/platform/topology/README.md
         */
        [[nodiscard]] SUSHIRUNTIME_API std::optional<unsigned>
        local_numa_os_index(hwloc_topology_t topology, hwloc_obj_t object);
    } // namespace Topology
} // namespace SushiRuntime
