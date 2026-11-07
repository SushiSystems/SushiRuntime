/**************************************************************************/
/* test_dispatch_executor.cpp                                             */
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
 * @file test_dispatch_executor.cpp
 * @brief Tests DispatchExecutor on a host runtime: admission, kernels, outputs, order and the replies.
 * @see tests/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/dispatch_executor/dispatch_executor.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/context_access/context_access.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/distributed/buffer_view.hpp>
#include <SushiRuntime/distributed/kernel_registry.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/transport/message_type.hpp>
#include <SushiRuntime/wire/wire_frame.hpp>

#include "support/executor_fabric.hpp"
#include "support/heap_cache_allocator.hpp"
#include "support/host_runtime.hpp"
#include "support/worker_wire.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;
using namespace TestSupport::WorkerWire;

namespace
{
    /** @brief Holds the cache budget most cases run with. */
    constexpr std::uint64_t K_CACHE_BYTES = 1024 * 1024;

    /** @brief Holds the cache budget of the case whose cache cannot hold an output. */
    constexpr std::uint64_t K_TINY_CACHE_BYTES = 32;

    /** @brief Waits for no chunk: a pull whose outcome is already set returns at once. */
    constexpr std::chrono::milliseconds K_NO_WAIT{0};

    /**
     * @brief Runs an executor on a host runtime, with the master's end of the connection read
     *        through an ExecutorFabric.
     */
    class Small_DispatchExecutor : public TestSupport::HostRuntimeTest
    {
        protected:
            /** @brief Builds the executor over a cache of @p cache_bytes. */
            explicit Small_DispatchExecutor(std::uint64_t cache_bytes = K_CACHE_BYTES)
                : kernels_(make_sushi<KernelRegistry>()), cache_(heap_.port(), cache_bytes),
                  executor_(kernels_, cache_, *fabric_.worker(),
                            Internal::ContextAccess::context(runtime()).devices(),
                            runtime().graph(), runtime().interop(), DispatchExecutorOptions{})
            {

            }

            /** @brief Connects the master and the worker endpoints of the fabric. */
            void SetUp() override
            {
                fabric_.start_master();
                fabric_.start_worker_recording();
                ASSERT_FALSE(fabric_.worker_events().connected().empty());
                master_peer_ = fabric_.worker_events().connected().back();
            }

            /** @brief Queues a dispatch from the master and expects it to be accepted. */
            void enqueue(const Wire::TaskDispatchMessage& dispatch, std::uint32_t chunk_bytes = 0)
            {
                ASSERT_EQ(executor_.enqueue(master_peer_, dispatch, chunk_bytes),
                          EnqueueStatus::Queued);
            }

            /** @brief Runs one batch and lets every reply reach the master. */
            void step()
            {
                ASSERT_TRUE(executor_.step());
                fabric_.settle();
            }

            /** @brief Queues a dispatch, runs its batch and lets the replies through. */
            void run(const Wire::TaskDispatchMessage& dispatch, std::uint32_t chunk_bytes = 0)
            {
                enqueue(dispatch, chunk_bytes);
                step();
            }

            /** @brief Puts an input the master sent into the cache, valid and unpinned. */
            void preload(const BufferHandle& handle, const std::vector<std::byte>& bytes)
            {
                BufferView view;
                ASSERT_EQ(cache_.begin_input(handle, view), CacheStatus::NeedsPull);
                cache_.accept_chunk(pull_chunk(handle, 0, bytes.size(), bytes), true);
                ASSERT_EQ(cache_.wait_input(handle, K_NO_WAIT, view), CacheStatus::Ready);
                cache_.unpin(handle.id);
            }

            /** @brief Returns the frames of a type the master received, in order. */
            [[nodiscard]] std::vector<RecordedFrame> received(MessageType type)
            {
                return fabric_.master_events().frames_of(type);
            }

            /** @brief Returns the n-th TASK_RESULT the master received, counted from 0. */
            [[nodiscard]] Wire::TaskResultMessage result(std::size_t index)
            {
                const std::vector<RecordedFrame> frames = received(MessageType::TaskResult);
                if (index >= frames.size())
                {
                    ADD_FAILURE() << "no TASK_RESULT number " << index;
                    return Wire::TaskResultMessage{};
                }
                return decode<Wire::TaskResultMessage>(frames[index]);
            }

            /** @brief Returns the data of the first output chunk a task sent. */
            [[nodiscard]] std::vector<std::byte> first_output_of(std::uint64_t task_id)
            {
                for (const RecordedFrame& frame : received(MessageType::BufferChunk))
                {
                    const Wire::BufferChunkMessage chunk = decode<Wire::BufferChunkMessage>(frame);
                    if (chunk.task_id == task_id)
                        return std::vector<std::byte>(chunk.data.begin(), chunk.data.end());
                }
                ADD_FAILURE() << "task " << task_id << " sent no output chunk";
                return {};
            }

            /// @brief Serves the cache's blocks.
            TestSupport::HeapCacheAllocator heap_;

            /// @brief The two endpoints and the clock they read.
            TestSupport::ExecutorFabric fabric_;

            /// @brief The kernels the executor can run.
            sushi_ptr<KernelRegistry> kernels_;

            /// @brief The buffers of the tasks.
            WorkerBufferCache cache_;

            /// @brief The executor under test.
            DispatchExecutor executor_;

            /// @brief The name the worker endpoint gives its connection to the master.
            PeerID master_peer_;
    };

    /** @brief Runs the executor over a cache that holds 32 bytes. */
    class Small_DispatchExecutorTinyCache : public Small_DispatchExecutor
    {
        protected:
            /** @brief Builds the executor over the tiny cache. */
            Small_DispatchExecutorTinyCache() : Small_DispatchExecutor(K_TINY_CACHE_BYTES)
            {

            }
    };

    /** @brief Returns a host kernel that counts its runs and writes nothing. */
    KernelRegistry::HostKernel counting_host_kernel(int& calls)
    {
        return [&calls](span<const BufferView>, span<const BufferView>, const NodeOptions&)
        {
            ++calls;
        };
    }
} // namespace

/** @brief Tests that an output the cache cannot hold fails the task, and the next one runs. */
TEST_F(Small_DispatchExecutorTinyCache, OutputAllocationFailureFailsTheTask)
{
    int calls = 0;
    const OperationID op(12);
    kernels_->register_host_kernel(op, counting_host_kernel(calls));

    run(dispatch_of(1, op, {}, {handle_of(7, 1, 64)}));
    EXPECT_EQ(result(0).task_id, 1u);
    EXPECT_EQ(result(0).status, Wire::TaskResultStatus::OutputAllocationFailed);
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(received(MessageType::BufferChunk).size(), 0u);

    run(dispatch_of(2, op, {}, {handle_of(7, 2, 16)}));
    EXPECT_EQ(result(1).status, Wire::TaskResultStatus::Succeeded);
    EXPECT_EQ(executor_.statistics().tasks_failed_output_allocation, 1u);
    EXPECT_EQ(executor_.statistics().tasks_run, 1u);
}

/** @brief Tests that a kernel sees each buffer's size and the output leaves in chunks. */
TEST_F(Small_DispatchExecutor, KernelSeesBufferSizesAndTheOutputLeavesInChunks)
{
    std::size_t input_size = 0;
    std::size_t output_size = 0;
    const OperationID op(13);
    kernels_->register_host_kernel(
        op, [&input_size, &output_size](span<const BufferView> inputs,
                                        span<const BufferView> outputs, const NodeOptions&)
        {
            input_size = inputs[0].size_bytes;
            output_size = outputs[0].size_bytes;
            std::memset(outputs[0].data, 0, outputs[0].size_bytes);
            std::memcpy(outputs[0].data, inputs[0].data, inputs[0].size_bytes);
        });
    const BufferHandle input = handle_of(3, 5, 12);
    const std::vector<std::byte> bytes = counting_bytes(12, 1);
    preload(input, bytes);

    run(dispatch_of(1, op, {input}, {handle_of(4, 1, 20)}), 8);

    EXPECT_EQ(result(0).status, Wire::TaskResultStatus::Succeeded);
    EXPECT_EQ(input_size, 12u);
    EXPECT_EQ(output_size, 20u);
    const std::vector<RecordedFrame> chunks = received(MessageType::BufferChunk);
    ASSERT_EQ(chunks.size(), 3u);
    const Wire::BufferChunkMessage first = decode<Wire::BufferChunkMessage>(chunks[0]);
    EXPECT_EQ(first.purpose, Wire::ChunkPurpose::TaskOutput);
    EXPECT_EQ(first.handle.size_bytes, 20u);
    ASSERT_EQ(first.data.size(), 8u);
    EXPECT_EQ(std::memcmp(first.data.data(), bytes.data(), 8), 0);
    EXPECT_EQ(received(MessageType::BufferRequest).size(), 0u) << "a cached input was pulled";
}

/** @brief Tests that an output's chunks come before its TASK_RESULT, which ends the stream. */
TEST_F(Small_DispatchExecutor, OutputStreamEndsWithTheResult)
{
    const OperationID op(16);
    const std::vector<std::byte> bytes = counting_bytes(10, 100);
    kernels_->register_host_kernel(
        op, [&bytes](span<const BufferView>, span<const BufferView> outputs, const NodeOptions&)
        {
            std::memcpy(outputs[0].data, bytes.data(), bytes.size());
        });
    const BufferHandle output = handle_of(5, 2, 10);

    run(dispatch_of(9, op, {}, {output}), 4);

    std::vector<RecordedFrame> frames;
    for (const RecordedFrame& frame : fabric_.master_events().frames())
    {
        if (frame.type == MessageType::TaskAccepted || frame.type == MessageType::BufferChunk ||
            frame.type == MessageType::TaskResult)
            frames.push_back(frame);
    }
    ASSERT_EQ(frames.size(), 5u);
    EXPECT_EQ(frames[0].type, MessageType::TaskAccepted);
    const std::size_t lengths[] = {4, 4, 2};
    std::uint64_t offset = 0;
    for (std::size_t index = 0; index < 3; ++index)
    {
        const RecordedFrame& frame = frames[index + 1];
        ASSERT_EQ(frame.type, MessageType::BufferChunk);
        const Wire::BufferChunkMessage chunk = decode<Wire::BufferChunkMessage>(frame);
        EXPECT_EQ(chunk.task_id, 9u);
        EXPECT_EQ(chunk.handle.version, output.version);
        EXPECT_EQ(chunk.offset, offset);
        ASSERT_EQ(chunk.data.size(), lengths[index]);
        EXPECT_EQ(std::memcmp(chunk.data.data(), bytes.data() + offset, lengths[index]), 0);
        EXPECT_EQ(frame.flags, index == 2 ? Wire::K_FRAME_FLAG_LAST_CHUNK : 0);
        offset += lengths[index];
    }
    ASSERT_EQ(frames[4].type, MessageType::TaskResult);
    const Wire::TaskResultMessage finished = decode<Wire::TaskResultMessage>(frames[4]);
    EXPECT_EQ(finished.status, Wire::TaskResultStatus::Succeeded);
    ASSERT_EQ(finished.outputs.size(), 1u);
    EXPECT_EQ(finished.outputs[0].id, output.id);
}

/** @brief Tests that a release before the stream is read frees the cache, and the stream keeps its bytes. */
TEST_F(Small_DispatchExecutor, AReleasedOutputStillStreamsItsOwnCopy)
{
    const OperationID op(16);
    kernels_->register_host_kernel(
        op, [](span<const BufferView>, span<const BufferView> outputs, const NodeOptions&)
        {
            std::memset(outputs[0].data, 0x5a, outputs[0].size_bytes);
        });
    enqueue(dispatch_of(9, op, {}, {handle_of(5, 2, 10)}), 4);
    ASSERT_TRUE(executor_.step());

    cache_.release(GlobalBufferID{5});
    EXPECT_EQ(cache_.bytes(), 0u) << "the stream kept the cached output pinned";
    fabric_.settle();

    EXPECT_EQ(result(0).status, Wire::TaskResultStatus::Succeeded);
    EXPECT_EQ(first_output_of(9), std::vector<std::byte>(4, std::byte{0x5a}));
}

/** @brief Tests that a dispatch naming a device the worker lacks fails as KernelFailed. */
TEST_F(Small_DispatchExecutor, DispatchNamingAMissingDeviceFailsTheTask)
{
    int calls = 0;
    const OperationID op(17);
    kernels_->register_host_kernel(op, counting_host_kernel(calls));
    Wire::TaskDispatchMessage missing = dispatch_of(1, op, {}, {handle_of(1, 1, 16)});
    missing.device_index = static_cast<std::uint32_t>(
        Internal::ContextAccess::context(runtime()).devices().count());

    run(missing);
    EXPECT_EQ(result(0).status, Wire::TaskResultStatus::KernelFailed);
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(received(MessageType::BufferChunk).size(), 0u);

    run(dispatch_of(2, op, {}, {handle_of(1, 2, 16)}));
    EXPECT_EQ(result(1).status, Wire::TaskResultStatus::Succeeded);
    EXPECT_EQ(calls, 1);
}

/** @brief Tests that a generic lambda registers as a host kernel and runs. */
TEST_F(Small_DispatchExecutor, GenericLambdaRegistersAsAHostKernel)
{
    const OperationID op(14);
    kernels_->register_host_kernel(
        op, [](auto /*inputs*/, auto outputs, const auto& /*metadata*/)
        {
            std::memset(outputs[0].data, 0x5a, outputs[0].size_bytes);
        });

    run(dispatch_of(1, op, {}, {handle_of(8, 1, 4)}));

    EXPECT_EQ(result(0).status, Wire::TaskResultStatus::Succeeded);
    EXPECT_EQ(first_output_of(1), std::vector<std::byte>(4, std::byte{0x5a}));
}

/** @brief Tests that a kernel that throws fails its task and the next task still runs. */
TEST_F(Small_DispatchExecutor, KernelThatThrowsFailsTheTaskAndTheWorkerLives)
{
    int calls = 0;
    const OperationID op(15);
    kernels_->register_host_kernel(
        op, [&calls](span<const BufferView>, span<const BufferView>, const NodeOptions&)
        {
            if (++calls == 1)
                throw std::runtime_error("kernel refused");
        });

    run(dispatch_of(1, op, {}, {handle_of(5, 1, 16)}));
    EXPECT_EQ(result(0).status, Wire::TaskResultStatus::KernelFailed);
    EXPECT_TRUE(result(0).outputs.empty());
    EXPECT_EQ(received(MessageType::BufferChunk).size(), 0u);

    run(dispatch_of(2, op, {}, {handle_of(5, 2, 16)}));
    EXPECT_EQ(result(1).status, Wire::TaskResultStatus::Succeeded);
    EXPECT_EQ(executor_.statistics().tasks_failed_kernel, 1u);
    EXPECT_EQ(executor_.statistics().tasks_run, 1u);
}

/** @brief Tests that two dispatches of one batch that write one buffer run in order. */
TEST_F(Small_DispatchExecutor, DispatchesWritingOneBufferRunOneAfterTheOther)
{
    std::vector<std::size_t> order;
    const OperationID op(20);
    kernels_->register_host_kernel(
        op, [&order](span<const BufferView> inputs, span<const BufferView> outputs,
                     const NodeOptions&)
        {
            const std::byte fill = inputs.size() == 1 ? std::byte{0x11} : std::byte{0x22};
            std::memset(outputs[0].data, static_cast<int>(fill), outputs[0].size_bytes);
            order.push_back(inputs.size());
        });
    const BufferHandle input = handle_of(3, 1, 8);
    preload(input, counting_bytes(8, 1));

    enqueue(dispatch_of(1, op, {input}, {handle_of(6, 1, 16)}));
    enqueue(dispatch_of(2, op, {}, {handle_of(6, 2, 16)}));
    step();

    EXPECT_EQ(result(0).status, Wire::TaskResultStatus::Succeeded);
    EXPECT_EQ(result(1).status, Wire::TaskResultStatus::Succeeded);
    EXPECT_EQ(order, (std::vector<std::size_t>{1, 0})) << "the writers ran out of order";
    EXPECT_EQ(first_output_of(1), std::vector<std::byte>(16, std::byte{0x11}))
        << "the first task sent bytes the second wrote";
    EXPECT_EQ(first_output_of(2), std::vector<std::byte>(16, std::byte{0x22}));
}

/** @brief Tests that a dispatch reading an output of a queued one runs after it, in the next batch. */
TEST_F(Small_DispatchExecutor, ADispatchReadingAQueuedOutputRunsAfterIt)
{
    const OperationID write(22);
    const OperationID copy(23);
    kernels_->register_host_kernel(
        write, [](span<const BufferView>, span<const BufferView> outputs, const NodeOptions&)
        {
            std::memset(outputs[0].data, 0x33, outputs[0].size_bytes);
        });
    kernels_->register_host_kernel(
        copy, [](span<const BufferView> inputs, span<const BufferView> outputs,
                 const NodeOptions&)
        {
            std::memcpy(outputs[0].data, inputs[0].data, outputs[0].size_bytes);
        });
    const BufferHandle produced = handle_of(7, 1, 16);

    enqueue(dispatch_of(1, write, {}, {produced}));
    enqueue(dispatch_of(2, copy, {produced}, {handle_of(8, 1, 16)}));
    step();
    EXPECT_EQ(received(MessageType::TaskResult).size(), 1u) << "the reader joined the writer's batch";
    step();

    EXPECT_EQ(result(1).status, Wire::TaskResultStatus::Succeeded);
    EXPECT_EQ(received(MessageType::BufferRequest).size(), 0u)
        << "the reader asked the master for an output this worker makes";
    EXPECT_EQ(first_output_of(2), std::vector<std::byte>(16, std::byte{0x33}));
}
