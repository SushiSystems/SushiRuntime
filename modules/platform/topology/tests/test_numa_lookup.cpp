/**************************************************************************/
/* test_numa_lookup.cpp                                                   */
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
 * @file test_numa_lookup.cpp
 * @brief Tests that local_numa_os_index names the NUMA node of each PU of a synthetic machine.
 * @author Mustafa Garip
 */

#include <optional>

#include <gtest/gtest.h>
#include <hwloc.h>

#include "numa_lookup.hpp"

using namespace SushiRuntime::Topology;

namespace
{
    /** @brief Loads a synthetic hwloc topology and destroys it on scope exit. */
    class SyntheticTopology final
    {
        public:
            /** @brief Loads the machine a hwloc synthetic description spells; get() is null on refusal. */
            explicit SyntheticTopology(const char* description) noexcept
            {
                if (hwloc_topology_init(&topology_) < 0)
                {
                    topology_ = nullptr;
                    return;
                }
                if (hwloc_topology_set_synthetic(topology_, description) < 0 ||
                    hwloc_topology_load(topology_) < 0)
                {
                    hwloc_topology_destroy(topology_);
                    topology_ = nullptr;
                }
            }

            /** @brief Destroys the topology, if one was loaded. */
            ~SyntheticTopology()
            {
                if (topology_ != nullptr)
                    hwloc_topology_destroy(topology_);
            }

            SyntheticTopology(const SyntheticTopology&) = delete;
            SyntheticTopology& operator=(const SyntheticTopology&) = delete;
            SyntheticTopology(SyntheticTopology&&) = delete;
            SyntheticTopology& operator=(SyntheticTopology&&) = delete;

            /** @brief Returns the loaded topology, or null when hwloc refused the description. */
            [[nodiscard]] hwloc_topology_t get() const noexcept { return topology_; }

        private:
            /** @brief Holds the loaded topology, or null. */
            hwloc_topology_t topology_ = nullptr;
    };
} // namespace

TEST(Unit_NumaLookup, EveryPuOfATwoNodeMachineMapsToItsOwnNode)
{
    const SyntheticTopology machine("NUMANode:2 Core:2 PU:1");
    ASSERT_NE(machine.get(), nullptr);
    ASSERT_EQ(hwloc_get_nbobjs_by_type(machine.get(), HWLOC_OBJ_NUMANODE), 2);
    ASSERT_EQ(hwloc_get_nbobjs_by_type(machine.get(), HWLOC_OBJ_PU), 4);

    for (unsigned pu_index = 0; pu_index < 4; ++pu_index)
    {
        const hwloc_obj_t pu = hwloc_get_obj_by_type(machine.get(), HWLOC_OBJ_PU, pu_index);
        ASSERT_NE(pu, nullptr);

        const std::optional<unsigned> node = local_numa_os_index(machine.get(), pu);
        ASSERT_TRUE(node.has_value()) << "PU " << pu_index << " has no NUMA node";
        EXPECT_EQ(*node, pu_index / 2) << "PU " << pu_index;
    }
}

TEST(Unit_NumaLookup, ANumaNodeIsLocalToItself)
{
    const SyntheticTopology machine("NUMANode:2 Core:2 PU:1");
    ASSERT_NE(machine.get(), nullptr);

    const hwloc_obj_t second_node = hwloc_get_obj_by_type(machine.get(), HWLOC_OBJ_NUMANODE, 1);
    ASSERT_NE(second_node, nullptr);

    const std::optional<unsigned> node = local_numa_os_index(machine.get(), second_node);
    ASSERT_TRUE(node.has_value());
    EXPECT_EQ(*node, second_node->os_index);
}

TEST(Unit_NumaLookup, ANullObjectHasNoNode)
{
    const SyntheticTopology machine("NUMANode:2 Core:2 PU:1");
    ASSERT_NE(machine.get(), nullptr);

    EXPECT_FALSE(local_numa_os_index(machine.get(), nullptr).has_value());
}
