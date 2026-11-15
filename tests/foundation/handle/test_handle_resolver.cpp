/**************************************************************************/
/* test_handle_resolver.cpp                                               */
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
 * @file test_handle_resolver.cpp
 * @brief Tests the HandleResolver contract through a small fake resolver.
 * @see tests/foundation/handle/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <map>
#include <utility>

#include <gtest/gtest.h>

#include <SushiRuntime/handle/handle_resolver.hpp>

using SushiRuntime::GlobalHandle;
using SushiRuntime::HandleFlags;
using SushiRuntime::HandleKind;
using SushiRuntime::HandleResolver;
using SushiRuntime::HandleStatus;
using SushiRuntime::MemorySpaceID;
using SushiRuntime::ResolveOutcome;

namespace
{
    /** @brief Resolves handles of one universe from tables of slot generations and instances. */
    class FakeHandleResolver final : public HandleResolver
    {
        public:
            /** @brief Builds a resolver of universe with no slot. */
            explicit FakeHandleResolver(std::uint32_t universe) : universe_(universe)
            {

            }

            /** @brief Sets the current generation of slot. */
            void set_generation(std::uint32_t slot, std::uint32_t generation)
            {
                generations_[slot] = generation;
            }

            /** @brief Places the instance of slot on space at address. */
            void place(std::uint32_t slot, MemorySpaceID space, void* address)
            {
                instances_[{slot, space}] = address;
            }

            /** @brief Returns the instance of handle on space, or why there is none. */
            ResolveOutcome resolve(GlobalHandle handle, MemorySpaceID space) const override
            {
                if (handle.is_null())
                    return {nullptr, HandleStatus::NullHandle};
                if (handle.universe() != universe_)
                    return {nullptr, HandleStatus::ForeignUniverse};
                if (is_stale(handle))
                    return {nullptr, HandleStatus::StaleHandle};
                const auto instance = instances_.find({handle.slot(), space});
                if (instance == instances_.end())
                    return {nullptr, HandleStatus::NotResident};
                return {instance->second, HandleStatus::Ok};
            }

            /** @brief Returns whether handle's generation differs from its slot's current one. */
            bool is_stale(GlobalHandle handle) const override
            {
                const auto generation = generations_.find(handle.slot());
                return generation == generations_.end() ||
                       generation->second != handle.generation();
            }

        private:
            /// @brief Universe the resolver accepts.
            std::uint32_t universe_;

            /// @brief Current generation of each known slot.
            std::map<std::uint32_t, std::uint32_t> generations_;

            /// @brief Address of each slot's instance on each space.
            std::map<std::pair<std::uint32_t, MemorySpaceID>, void*> instances_;
    };

    /** @brief Stands in for one instance; only its address is used. */
    int instance_on_device = 0;
}

TEST(Small_HandleResolver, ResolvesACurrentHandleOnItsSpace)
{
    FakeHandleResolver fake(1);
    fake.set_generation(4, 2);
    fake.place(4, MemorySpaceID(7), &instance_on_device);
    const HandleResolver& resolver = fake;
    const GlobalHandle handle(1, 0, HandleKind::Buffer, HandleFlags::None, 2, 4);

    const ResolveOutcome outcome = resolver.resolve(handle, MemorySpaceID(7));
    EXPECT_EQ(outcome.status, HandleStatus::Ok);
    EXPECT_EQ(outcome.address, &instance_on_device);
    EXPECT_FALSE(resolver.is_stale(handle));

    const ResolveOutcome elsewhere = resolver.resolve(handle, MemorySpaceID(8));
    EXPECT_EQ(elsewhere.status, HandleStatus::NotResident);
    EXPECT_EQ(elsewhere.address, nullptr);
}

TEST(Small_HandleResolver, RefusesNullStaleAndForeignHandlesWithANullAddress)
{
    FakeHandleResolver fake(1);
    fake.set_generation(4, 3);
    fake.place(4, MemorySpaceID(7), &instance_on_device);
    const HandleResolver& resolver = fake;
    const GlobalHandle stale(1, 0, HandleKind::Buffer, HandleFlags::None, 2, 4);
    const GlobalHandle foreign(9, 0, HandleKind::Buffer, HandleFlags::None, 3, 4);

    EXPECT_TRUE(resolver.is_stale(stale));
    const std::pair<GlobalHandle, HandleStatus> rows[] = {
        {GlobalHandle::null(), HandleStatus::NullHandle},
        {stale, HandleStatus::StaleHandle},
        {foreign, HandleStatus::ForeignUniverse}};
    for (const auto& row : rows)
    {
        const ResolveOutcome outcome = resolver.resolve(row.first, MemorySpaceID(7));
        EXPECT_EQ(outcome.status, row.second);
        EXPECT_EQ(outcome.address, nullptr);
    }
}
