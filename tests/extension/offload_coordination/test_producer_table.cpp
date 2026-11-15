/**************************************************************************/
/* test_producer_table.cpp                                                */
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
 * @file test_producer_table.cpp
 * @brief Tests ProducerTable: records, settling, newer versions and what a lost worker held.
 * @author Mustafa Garip
 * @see tests/extension/offload_coordination/README.md
 */

#include <cstdint>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/task/node.hpp>

#include <SushiRuntime/offload_coordination/producer_table.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

namespace
{
    /** @brief Returns a master handle on slot @p slot. */
    GlobalHandle handle_at(std::uint32_t slot)
    {
        return GlobalHandle(1, 0, HandleKind::Buffer, HandleFlags::None, 1, slot);
    }

    /** @brief Returns the wire handle of slot @p slot at @p version. */
    BufferHandle wire(std::uint32_t slot, std::uint32_t version)
    {
        return BufferHandle{handle_at(slot), version, 64};
    }

    /** @brief Returns a span over @p handles. */
    span<const BufferHandle> view(const std::vector<BufferHandle>& handles)
    {
        return span<const BufferHandle>(handles.data(), handles.size());
    }
} // namespace

/** @brief Tests that a remote record names the node, task and worker, and settling moves it home. */
TEST(Small_ProducerTable, SettlingAWholeResultNamesTheMasterAsHolder)
{
    ProducerTable table;
    const sushi_ptr<Internal::Graph::Node> node = make_sushi<Internal::Graph::Node>();
    const std::vector<BufferHandle> outputs{wire(3, 2)};

    table.record_remote(view(outputs), node.get(), 7, PeerID{4});
    std::optional<ProducerRecord> record = table.producer_of(handle_at(3));
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->node, node.get());
    EXPECT_EQ(record->task_id, 7u);
    EXPECT_EQ(record->version, 2u);
    EXPECT_EQ(record->holder.value, 4u);

    table.settle(7, view(outputs));
    record = table.producer_of(handle_at(3));
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->holder.value, 0u);
    EXPECT_EQ(record->node, node.get());
}

/** @brief Tests that a settle of another task or version leaves the record with its worker. */
TEST(Small_ProducerTable, SettleOfAnotherTaskOrVersionChangesNothing)
{
    ProducerTable table;
    const std::vector<BufferHandle> outputs{wire(1, 3)};
    table.record_remote(view(outputs), nullptr, 9, PeerID{2});

    table.settle(8, view(outputs));
    const std::vector<BufferHandle> old_version{wire(1, 2)};
    table.settle(9, view(old_version));

    EXPECT_EQ(table.producer_of(handle_at(1))->holder.value, 2u);
}

/** @brief Tests that an older version never replaces the record of a newer one. */
TEST(Small_ProducerTable, AnOlderVersionDoesNotReplaceANewerRecord)
{
    ProducerTable table;
    const std::vector<BufferHandle> newer{wire(5, 4)};
    const std::vector<BufferHandle> older{wire(5, 3)};
    table.record_local(view(newer), nullptr);
    table.record_remote(view(older), nullptr, 11, PeerID{1});

    const std::optional<ProducerRecord> record = table.producer_of(handle_at(5));
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->version, 4u);
    EXPECT_EQ(record->holder.value, 0u);
}

/** @brief Tests that lose() names only what the lost worker alone held, in handle order, once. */
TEST(Small_ProducerTable, LoseNamesTheWorkersVersionsInHandleOrderOnce)
{
    ProducerTable table;
    const std::vector<BufferHandle> first{wire(9, 2)};
    const std::vector<BufferHandle> second{wire(2, 2)};
    const std::vector<BufferHandle> other_worker{wire(4, 2)};
    const std::vector<BufferHandle> settled{wire(6, 2)};
    table.record_remote(view(first), nullptr, 1, PeerID{3});
    table.record_remote(view(second), nullptr, 2, PeerID{3});
    table.record_remote(view(other_worker), nullptr, 3, PeerID{5});
    table.record_remote(view(settled), nullptr, 4, PeerID{3});
    table.settle(4, view(settled));

    std::vector<LostInstance> lost;
    table.lose(PeerID{3}, lost);
    ASSERT_EQ(lost.size(), 2u);
    EXPECT_EQ(lost[0].handle, handle_at(2));
    EXPECT_EQ(lost[0].producer.task_id, 2u);
    EXPECT_EQ(lost[1].handle, handle_at(9));
    EXPECT_EQ(lost[1].producer.task_id, 1u);

    table.lose(PeerID{3}, lost);
    EXPECT_TRUE(lost.empty());
    EXPECT_EQ(table.producer_of(handle_at(4))->holder.value, 5u);
}

/** @brief Tests that a forgotten handle has no producer and is never reported lost. */
TEST(Small_ProducerTable, ForgottenHandleHasNoProducer)
{
    ProducerTable table;
    const std::vector<BufferHandle> outputs{wire(8, 2)};
    table.record_remote(view(outputs), nullptr, 1, PeerID{2});
    table.forget(handle_at(8));

    EXPECT_FALSE(table.producer_of(handle_at(8)).has_value());
    std::vector<LostInstance> lost;
    table.lose(PeerID{2}, lost);
    EXPECT_TRUE(lost.empty());
}
