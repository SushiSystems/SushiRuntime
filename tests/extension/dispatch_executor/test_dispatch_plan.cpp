/**************************************************************************/
/* test_dispatch_plan.cpp                                                 */
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
 * @file test_dispatch_plan.cpp
 * @brief Tests admit_dispatch(), judge_job(), reads_a_written_buffer() and usable_chunk_bytes().
 * @see tests/extension/dispatch_executor/README.md
 * @author Mustafa Garip
 */

#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/dispatch_executor/dispatch_plan.hpp>
#include <SushiRuntime/wire/wire_frame.hpp>
#include <SushiRuntime/wire/wire_messages.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

namespace
{
    /** @brief Returns a handle of buffer @p id. */
    BufferHandle handle_of(std::uint64_t id)
    {
        BufferHandle handle;
        handle.id = GlobalBufferID{id};
        return handle;
    }
} // namespace

/** @brief Tests that a dispatch is queued while the queue has room and stop() has not begun. */
TEST(Small_DispatchPlan, ADispatchIsQueuedWhileTheQueueHasRoom)
{
    EXPECT_EQ(admit_dispatch(0, 2, false), EnqueueStatus::Queued);
    EXPECT_EQ(admit_dispatch(1, 2, false), EnqueueStatus::Queued);
}

/** @brief Tests that a full queue refuses the dispatch so the master takes it back. */
TEST(Small_DispatchPlan, AFullQueueRefusesTheDispatch)
{
    EXPECT_EQ(admit_dispatch(2, 2, false), EnqueueStatus::QueueFull);
    EXPECT_EQ(admit_dispatch(3, 2, false), EnqueueStatus::QueueFull);
}

/** @brief Tests that a stopping executor answers Stopping, even when the queue is full. */
TEST(Small_DispatchPlan, AStoppingExecutorAnswersStopping)
{
    EXPECT_EQ(admit_dispatch(0, 2, true), EnqueueStatus::Stopping);
    EXPECT_EQ(admit_dispatch(2, 2, true), EnqueueStatus::Stopping);
}

/** @brief Tests that a job with a known kernel and a usable device runs. */
TEST(Small_DispatchPlan, AJobWithAKernelAndAUsableDeviceRuns)
{
    EXPECT_EQ(judge_job(true, 1, 2, true), JobVerdict::Run);
}

/** @brief Tests that an unknown operation is named before the device is looked at. */
TEST(Small_DispatchPlan, AnUnknownOperationIsNamedBeforeTheDevice)
{
    EXPECT_EQ(judge_job(false, 9, 2, false), JobVerdict::UnknownOperation);
}

/** @brief Tests that a device the worker does not have is named. */
TEST(Small_DispatchPlan, ADeviceTheWorkerDoesNotHaveIsNamed)
{
    EXPECT_EQ(judge_job(true, 2, 2, false), JobVerdict::DeviceMissing);
    EXPECT_EQ(judge_job(true, 0, 0, false), JobVerdict::DeviceMissing);
}

/** @brief Tests that a device outside the cache's context is named. */
TEST(Small_DispatchPlan, ADeviceOutsideTheCacheContextIsNamed)
{
    EXPECT_EQ(judge_job(true, 1, 2, false), JobVerdict::DeviceOutsideCacheContext);
}

/** @brief Tests that a job reading a buffer the batch writes opens the next batch. */
TEST(Small_DispatchPlan, AJobReadingABufferTheBatchWritesOpensTheNextBatch)
{
    const std::vector<BufferHandle> inputs = {handle_of(1), handle_of(4)};
    const std::vector<GlobalBufferID> written = {GlobalBufferID{3}, GlobalBufferID{4}};

    EXPECT_TRUE(reads_a_written_buffer(span<const BufferHandle>(inputs.data(), inputs.size()),
                                       span<const GlobalBufferID>(written.data(), written.size())));
}

/** @brief Tests that a job reading nothing the batch writes joins it. */
TEST(Small_DispatchPlan, AJobReadingNothingTheBatchWritesJoinsIt)
{
    const std::vector<BufferHandle> inputs = {handle_of(1)};
    const std::vector<GlobalBufferID> written = {GlobalBufferID{3}};

    EXPECT_FALSE(reads_a_written_buffer(span<const BufferHandle>(inputs.data(), inputs.size()),
                                        span<const GlobalBufferID>(written.data(), written.size())));
    EXPECT_FALSE(reads_a_written_buffer(span<const BufferHandle>(), span<const GlobalBufferID>(written.data(), written.size())))
        << "a job with no input reads nothing";
    EXPECT_FALSE(reads_a_written_buffer(span<const BufferHandle>(inputs.data(), inputs.size()), span<const GlobalBufferID>()))
        << "an empty batch writes nothing";
}

/** @brief Tests that a chunk size of zero takes the default and a large one is cut to one frame. */
TEST(Small_DispatchPlan, TheChunkSizeIsTheDefaultOrWhatOneFrameCarries)
{
    EXPECT_EQ(usable_chunk_bytes(0), 256u * 1024u);
    EXPECT_EQ(usable_chunk_bytes(4096), 4096u);

    const std::uint32_t largest =
        Wire::K_MAX_FRAME_PAYLOAD - static_cast<std::uint32_t>(Wire::BufferChunkMessage::K_PREFIX_BYTES);
    EXPECT_EQ(usable_chunk_bytes(largest), largest);
    EXPECT_EQ(usable_chunk_bytes(largest + 1), largest);
}
