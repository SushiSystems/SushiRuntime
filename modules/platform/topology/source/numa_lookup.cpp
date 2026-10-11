/**************************************************************************/
/* numa_lookup.cpp                                                        */
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
 * @file numa_lookup.cpp
 * @brief Finds the NUMA node local to a hwloc object through the object's nodeset.
 * @author Mustafa Garip
 */

#include "numa_lookup.hpp"

namespace SushiRuntime
{
    namespace Topology
    {
        std::optional<unsigned> local_numa_os_index(hwloc_topology_t topology, hwloc_obj_t object)
        {
            for (hwloc_obj_t current = object; current != nullptr; current = current->parent)
            {
                // An I/O object carries no nodeset; its first non-I/O ancestor does.
                if (current->nodeset == nullptr)
                    continue;

                const int first_node = hwloc_bitmap_first(current->nodeset);
                if (first_node < 0)
                    continue;

                const hwloc_obj_t numa_node = hwloc_get_numanode_obj_by_os_index(
                    topology, static_cast<unsigned>(first_node));
                if (numa_node == nullptr)
                    return std::nullopt;
                return numa_node->os_index;
            }
            return std::nullopt;
        }
    } // namespace Topology
} // namespace SushiRuntime
