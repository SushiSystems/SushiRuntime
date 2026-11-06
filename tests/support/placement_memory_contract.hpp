/**************************************************************************/
/* placement_memory_contract.hpp                                          */
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
 * @file placement_memory_contract.hpp
 * @brief Declares the checks every PlacementMemory implementation must pass.
 * @see tests/engine/task/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <optional>

#include <gtest/gtest.h>

#include <SushiRuntime/task/placement_memory.hpp>

namespace TestSupport
{
    /** @brief Checks that an address nobody recorded has no recorded and no owning device. */
    inline void expect_unrecorded_address_has_no_device(const SushiRuntime::Internal::Graph::PlacementMemory& memory,
                                                        const void* address)
    {
        EXPECT_EQ(memory.recorded_device(address), std::nullopt);
        EXPECT_EQ(memory.owning_device(address), std::nullopt);
    }

    /** @brief Checks that an address recorded on @p device names it, recorded and owning. */
    inline void expect_recorded_address_names_device(const SushiRuntime::Internal::Graph::PlacementMemory& memory,
                                                     const void* address, std::size_t device)
    {
        EXPECT_EQ(memory.recorded_device(address), std::optional<std::size_t>(device));
        EXPECT_EQ(memory.owning_device(address), std::optional<std::size_t>(device));
    }

    /** @brief Checks that the memory spans @p count devices. */
    inline void expect_device_count(const SushiRuntime::Internal::Graph::PlacementMemory& memory, std::size_t count)
    {
        EXPECT_EQ(memory.device_count(), count);
    }

    /** @brief Checks that devices @p first and @p second share a SYCL context. */
    inline void expect_devices_share_a_context(const SushiRuntime::Internal::Graph::PlacementMemory& memory,
                                               std::size_t first, std::size_t second)
    {
        EXPECT_TRUE(memory.shares_context(first, second));
    }
} // namespace TestSupport
