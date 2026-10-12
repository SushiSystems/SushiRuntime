/**************************************************************************/
/* test_worker_bricks.cpp                                                 */
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
 * @file test_worker_bricks.cpp
 * @brief Tests the worker's cache, deduplicator, master link and executor over a fake transport.
 * @author Mustafa Garip
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>
#include <gtest/gtest.h>

#include <sycl/sycl.hpp>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/distributed/buffer_view.hpp>
#include <SushiRuntime/distributed/transport.hpp>
#include <SushiRuntime/distributed/kernel_registry.hpp>

#include "dispatch_deduplicator.hpp"
#include "dispatch_executor.hpp"
#include "handshake.hpp"
#include "master_link.hpp"
#include "wire_frame.hpp"
#include "wire_messages.hpp"
#include "worker_buffer_cache.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

namespace
{
    /** @brief Bounds every wait on a condition, so a broken brick fails a test and hangs none. */
    constexpr std::chrono::seconds K_WAIT{30};

    /** @brief Holds the connection every executor test dispatches on. */
    constexpr PeerID K_MASTER{1};

    /** @brief Holds one frame a brick handed to the transport. */
    struct SentFrame
    {
        PeerID peer;                    /**< The connection it was sent to. */
        MessageType type;               /**< Its type. */
        std::uint8_t flags = 0;         /**< Its flag byte. */
        std::vector<std::byte> payload; /**< Its payload. */
    };

    /**
     * @brief Records what the bricks send and close; opens no socket.
     *
     * A stream is pulled dry on the calling thread, or kept until the test releases it.
     */
    class FakeTransport final : public Transport
    {
        public:
            [[nodiscard]] TransportResult start(TransportEvents&) override
            {
                return TransportResult{};
            }

            void stop() noexcept override
            {
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    ++stops_;
                }
                changed_.notify_all();
            }

            [[nodiscard]] SendStatus send(PeerID peer, MessageType type,
                                          span<const std::byte> payload) override
            {
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    frames_.push_back(SentFrame{
                        peer, type, 0, std::vector<std::byte>(payload.begin(), payload.end())});
                }
                changed_.notify_all();
                return SendStatus::Queued;
            }

            [[nodiscard]] SendStatus send_stream(PeerID peer,
                                                 std::unique_ptr<FrameSource> source) override
            {
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    if (keep_streams_)
                    {
                        streams_.emplace_back(peer, std::move(source));
                        source.reset();
                    }
                }

                if (source)
                    pull_dry(peer, std::move(source));
                changed_.notify_all();
                return SendStatus::Queued;
            }

            void close_peer(PeerID peer) noexcept override
            {
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    closed_.push_back(peer);
                }
                changed_.notify_all();
            }

            [[nodiscard]] TransportExposure exposure() const noexcept override
            {
                return TransportExposure::Loopback;
            }

            [[nodiscard]] TransportStatistics statistics() const noexcept override
            {
                return TransportStatistics{};
            }

            /** @brief Keeps later streams unpulled until release_streams(). */
            void keep_streams()
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                keep_streams_ = true;
            }

            /** @brief Pulls every kept stream dry and destroys it, as a writer thread would. */
            void release_streams()
            {
                std::vector<std::pair<PeerID, std::unique_ptr<FrameSource>>> streams;
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    streams.swap(streams_);
                }
                for (auto& [peer, source] : streams)
                    pull_dry(peer, std::move(source));
                changed_.notify_all();
            }

            /** @brief Destroys every kept stream unpulled, as a closed peer would. */
            void drop_streams()
            {
                std::vector<std::pair<PeerID, std::unique_ptr<FrameSource>>> streams;
                const std::lock_guard<std::mutex> lock(mutex_);
                streams.swap(streams_);
            }

            /** @brief Waits until a number of frames of a type were sent. */
            [[nodiscard]] bool wait_for(MessageType type, std::size_t count)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                return changed_.wait_for(lock, K_WAIT, [this, type, count]
                                         {
                                             return count_locked(type) >= count;
                                         });
            }

            /** @brief Waits until a number of streams are kept. */
            [[nodiscard]] bool wait_for_streams(std::size_t count)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                return changed_.wait_for(lock, K_WAIT, [this, count]
                                         {
                                             return streams_.size() >= count;
                                         });
            }

            /** @brief Waits until a peer was closed. */
            [[nodiscard]] bool wait_for_close(PeerID peer)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                return changed_.wait_for(lock, K_WAIT, [this, peer]
                                         {
                                             return is_closed_locked(peer);
                                         });
            }

            /** @brief Waits until stop() was called. */
            [[nodiscard]] bool wait_for_stop()
            {
                std::unique_lock<std::mutex> lock(mutex_);
                return changed_.wait_for(lock, K_WAIT, [this] { return stops_ > 0; });
            }

            /** @brief Returns the number of stop() calls. */
            [[nodiscard]] std::size_t stop_count() const
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                return stops_;
            }

            /** @brief Returns whether a peer was closed. */
            [[nodiscard]] bool is_closed(PeerID peer) const
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                return is_closed_locked(peer);
            }

            /** @brief Returns the number of frames of a type sent so far. */
            [[nodiscard]] std::size_t count(MessageType type) const
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                return count_locked(type);
            }

            /** @brief Returns the frames of a type in the order they were sent. */
            [[nodiscard]] std::vector<SentFrame> frames_of(MessageType type) const
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                std::vector<SentFrame> frames;
                for (const SentFrame& frame : frames_)
                {
                    if (frame.type == type)
                        frames.push_back(frame);
                }
                return frames;
            }

            /** @brief Returns every frame in the order it was sent. */
            [[nodiscard]] std::vector<SentFrame> frames() const
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                return frames_;
            }

        private:
            /** @brief Records every frame of a stream, then destroys the stream. */
            void pull_dry(PeerID peer, std::unique_ptr<FrameSource> source)
            {
                OutboundFrame frame;
                for (;;)
                {
                    const FrameSourceStatus status = source->next(frame);
                    if (status == FrameSourceStatus::Finished)
                        break;

                    {
                        const std::lock_guard<std::mutex> lock(mutex_);
                        frames_.push_back(SentFrame{peer, frame.type, frame.flags, frame.payload});
                    }
                    if (status == FrameSourceStatus::FinalFrame)
                        break;
                }
            }

            /** @brief Returns the number of frames of a type; mutex held. */
            [[nodiscard]] std::size_t count_locked(MessageType type) const
            {
                return static_cast<std::size_t>(
                    std::count_if(frames_.begin(), frames_.end(), [type](const SentFrame& frame)
                                  {
                                      return frame.type == type;
                                  }));
            }

            /** @brief Returns whether a peer was closed; mutex held. */
            [[nodiscard]] bool is_closed_locked(PeerID peer) const
            {
                return std::find(closed_.begin(), closed_.end(), peer) != closed_.end();
            }

            /// @brief Guards every member below.
            mutable std::mutex mutex_;

            /// @brief Wakes the waits on a frame, a kept stream and a close.
            std::condition_variable changed_;

            /// @brief Every frame sent.
            std::vector<SentFrame> frames_;

            /// @brief Streams kept unpulled.
            std::vector<std::pair<PeerID, std::unique_ptr<FrameSource>>> streams_;

            /// @brief Peers closed.
            std::vector<PeerID> closed_;

            /// @brief Whether send_stream() keeps a stream.
            bool keep_streams_ = false;

            /// @brief Calls of stop().
            std::size_t stops_ = 0;
    };

    /**
     * @brief Hands the cache host memory, counts it and can be told to refuse.
     */
    class FakeAllocator final
    {
        public:
            /** @brief Returns the two callables a cache takes; this object outlives the cache. */
            [[nodiscard]] CacheAllocator callables()
            {
                CacheAllocator allocator;
                allocator.allocate = [this](std::size_t size_bytes) -> void*
                {
                    if (refuse_.load())
                        return nullptr;
                    ++live_;
                    return std::malloc(size_bytes);
                };
                allocator.release = [this](void* address)
                {
                    {
                        const std::lock_guard<std::mutex> lock(mutex_);
                        freed_.push_back(address);
                    }
                    --live_;
                    std::free(address);
                };
                return allocator;
            }

            /** @brief Makes every later allocation fail, or succeed again. */
            void refuse(bool value)
            {
                refuse_.store(value);
            }

            /** @brief Returns the number of allocations not freed. */
            [[nodiscard]] int live() const
            {
                return live_.load();
            }

            /** @brief Returns the addresses freed, in order. */
            [[nodiscard]] std::vector<void*> freed() const
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                return freed_;
            }

        private:
            /// @brief Guards freed_.
            mutable std::mutex mutex_;

            /// @brief The addresses freed.
            std::vector<void*> freed_;

            /// @brief Allocations not freed.
            std::atomic<int> live_{0};

            /// @brief Whether allocate returns null.
            std::atomic<bool> refuse_{false};
    };

    /** @brief Returns a handle. */
    BufferHandle handle_of(std::uint64_t id, std::uint32_t version, std::uint64_t size_bytes)
    {
        BufferHandle handle;
        handle.id = GlobalBufferID{id};
        handle.version = version;
        handle.size_bytes = size_bytes;
        return handle;
    }

    /** @brief Returns @p count bytes that count up from @p first. */
    std::vector<std::byte> counting_bytes(std::size_t count, std::uint8_t first)
    {
        std::vector<std::byte> bytes(count);
        for (std::size_t index = 0; index < count; ++index)
            bytes[index] = static_cast<std::byte>(first + index);
        return bytes;
    }

    /** @brief Returns a pull-reply chunk that carries part of @p bytes. */
    Wire::BufferChunkMessage pull_chunk(const BufferHandle& handle, std::uint64_t offset,
                                        std::size_t length, const std::vector<std::byte>& bytes)
    {
        Wire::BufferChunkMessage chunk;
        chunk.purpose = Wire::ChunkPurpose::PullReply;
        chunk.handle = handle;
        chunk.offset = offset;
        chunk.data = span<const std::byte>(bytes.data() + offset, length);
        return chunk;
    }

    /** @brief Decodes the payload of a recorded frame; the frame outlives the message. */
    template <typename Message>
    Message decode(const SentFrame& frame)
    {
        Message message;
        const Wire::DecodeResult result = Message::decode(
            span<const std::byte>(frame.payload.data(), frame.payload.size()), message);
        EXPECT_TRUE(result.ok()) << "payload failed to decode at offset " << result.offset;
        return message;
    }

    /** @brief Puts a committed, unpinned buffer into a cache and returns its address. */
    void* store(WorkerBufferCache& cache, std::uint64_t id, std::uint64_t size_bytes)
    {
        const BufferHandle handle = handle_of(id, 1, size_bytes);
        BufferView view;
        EXPECT_EQ(cache.pin_output(handle, view), CacheStatus::Ready);
        cache.commit_output(handle);
        cache.unpin(handle.id);
        return view.data;
    }

    /** @brief Returns a dispatch of one task. */
    Wire::TaskDispatchMessage dispatch_of(std::uint64_t task_id, OperationID op,
                                          std::vector<BufferHandle> inputs,
                                          std::vector<BufferHandle> outputs)
    {
        Wire::TaskDispatchMessage dispatch;
        dispatch.task_id = task_id;
        dispatch.operation = op;
        dispatch.inputs = std::move(inputs);
        dispatch.outputs = std::move(outputs);
        return dispatch;
    }

    /**
     * @brief Holds a cache, a registry and an executor over a fake transport and allocator.
     *
     * A test registers its kernels, then calls executor.start().
     */
    struct Worker
    {
        /** @brief Builds the bricks with a budget and the executor's bounds; no thread runs. */
        explicit Worker(std::uint64_t budget_bytes = 1024 * 1024,
                        const DispatchExecutorOptions& options = DispatchExecutorOptions{})
            : cache(allocator.callables(), budget_bytes),
              kernels(make_sushi<KernelRegistry>()),
              executor(kernels, cache, transport, queue, options)
        {

        }

        /** @brief Stops the exec thread and drops kept streams while the cache lives. */
        ~Worker()
        {
            executor.stop();
            transport.drop_streams();
        }

        Worker(const Worker&) = delete;
        Worker& operator=(const Worker&) = delete;
        Worker(Worker&&) = delete;
        Worker& operator=(Worker&&) = delete;

        /** @brief Returns the status of the n-th TASK_RESULT sent, counted from 0. */
        [[nodiscard]] Wire::TaskResultMessage result(std::size_t index) const
        {
            const std::vector<SentFrame> frames = transport.frames_of(MessageType::TaskResult);
            if (index >= frames.size())
            {
                ADD_FAILURE() << "no TASK_RESULT number " << index;
                return Wire::TaskResultMessage{};
            }
            return decode<Wire::TaskResultMessage>(frames[index]);
        }

        FakeAllocator allocator;           /**< The cache's memory. */
        FakeTransport transport;           /**< Records what the executor sends. */
        WorkerBufferCache cache;           /**< The buffers of the tasks. */
        sushi_ptr<KernelRegistry> kernels; /**< The kernels the test registers. */
        sycl::queue queue;                 /**< The queue handed to the kernels. */
        DispatchExecutor executor;         /**< The brick under test. */
    };

    /** @brief Returns a nonce whose bytes all hold @p value. */
    HandshakeNonce nonce_of(std::uint8_t value)
    {
        HandshakeNonce nonce{};
        nonce.bytes.fill(value);
        return nonce;
    }

    /** @brief Holds the nonce every link of these tests sends. */
    const HandshakeNonce K_WORKER_NONCE = nonce_of(0x11);

    /** @brief Holds the nonce the master of these tests answers with. */
    const HandshakeNonce K_MASTER_NONCE = nonce_of(0x22);

    /** @brief Returns 32 key bytes that all hold @p value. */
    std::vector<std::uint8_t> secret_of(std::uint8_t value)
    {
        return std::vector<std::uint8_t>(32, value);
    }

    /** @brief Returns the CHALLENGE a master holding @p secret sends for a worker's nonce. */
    Wire::ChallengeMessage challenge_of(const std::vector<std::uint8_t>& secret,
                                        std::uint64_t session_id,
                                        const HandshakeNonce& worker_nonce = K_WORKER_NONCE)
    {
        const Sha256Digest proof =
            compute_proof(HandshakeRole::Master,
                          span<const std::uint8_t>(secret.data(), secret.size()),
                          worker_nonce, K_MASTER_NONCE);

        Wire::ChallengeMessage challenge;
        std::memcpy(challenge.master_nonce.data(), K_MASTER_NONCE.bytes.data(),
                    challenge.master_nonce.size());
        std::memcpy(challenge.master_proof.data(), proof.data(), challenge.master_proof.size());
        challenge.session_id = session_id;
        return challenge;
    }

    /** @brief Returns link options with a name, two operations and a heartbeat interval. */
    MasterLinkOptions link_options(std::chrono::milliseconds heartbeat_interval)
    {
        MasterLinkOptions options;
        options.name = "worker-a";
        options.build_id = "test-build";
        options.ops = {OperationID(7), OperationID(9)};
        options.max_jobs = 16;
        options.cache_budget = 4096;
        options.heartbeat_interval = heartbeat_interval;
        return options;
    }

    /** @brief Returns a nonce source that always gives K_WORKER_NONCE. */
    NonceSource fixed_nonces()
    {
        return []
        {
            return K_WORKER_NONCE;
        };
    }

    /** @brief Runs a whole handshake on a new connection and returns what WELCOME changed. */
    LinkChange register_link(MasterLink& link, PeerID peer,
                             const std::vector<std::uint8_t>& secret, std::uint64_t session_id,
                             std::uint32_t liveness_timeout_ms)
    {
        link.on_connected(peer);
        EXPECT_EQ(link.on_challenge(peer, challenge_of(secret, session_id)), LinkChange::None);

        Wire::WelcomeMessage welcome;
        welcome.worker_id = peer.value;
        welcome.liveness_timeout_ms = liveness_timeout_ms;
        welcome.ping_interval_ms = 250;
        welcome.chunk_bytes = 4096;
        return link.on_welcome(peer, welcome);
    }
} // namespace

TEST(Unit_WorkerBufferCacheTest, ReleaseEvicts)
{
    FakeAllocator allocator;
    WorkerBufferCache cache(allocator.callables(), 1024);

    store(cache, 1, 64);
    EXPECT_EQ(cache.bytes(), 64u);

    cache.release(GlobalBufferID{1});
    EXPECT_EQ(cache.bytes(), 0u);
    EXPECT_EQ(cache.evictions(), 1u);
    EXPECT_EQ(allocator.live(), 0);

    const BufferHandle pinned = handle_of(2, 1, 64);
    BufferView view;
    ASSERT_EQ(cache.pin_output(pinned, view), CacheStatus::Ready);
    cache.release(pinned.id);
    EXPECT_EQ(cache.bytes(), 64u) << "a pinned buffer was freed by BUFFER_RELEASE";
    EXPECT_EQ(allocator.live(), 1);

    cache.unpin(pinned.id);
    EXPECT_EQ(cache.bytes(), 0u);
    EXPECT_EQ(allocator.live(), 0);
    EXPECT_EQ(cache.evictions(), 2u);
}

TEST(Unit_WorkerBufferCacheTest, BudgetEvictsLeastRecentlyUsedUnpinned)
{
    FakeAllocator allocator;
    WorkerBufferCache cache(allocator.callables(), 300);

    store(cache, 1, 100);
    void* const second = store(cache, 2, 100);
    store(cache, 3, 100);

    BufferView view;
    ASSERT_EQ(cache.begin_input(handle_of(1, 1, 100), view), CacheStatus::Ready);
    cache.unpin(GlobalBufferID{1});

    store(cache, 4, 100);

    EXPECT_EQ(allocator.freed(), std::vector<void*>{second});
    EXPECT_EQ(cache.evictions(), 1u);
    EXPECT_EQ(cache.bytes(), 300u);

    ASSERT_EQ(cache.begin_input(handle_of(1, 1, 100), view), CacheStatus::Ready)
        << "the buffer used last was evicted";
    cache.unpin(GlobalBufferID{1});
}

TEST(Unit_WorkerBufferCacheTest, PinnedEntryIsNotEvicted)
{
    FakeAllocator allocator;
    WorkerBufferCache cache(allocator.callables(), 200);

    const BufferHandle oldest = handle_of(1, 1, 100);
    BufferView view;
    ASSERT_EQ(cache.pin_output(oldest, view), CacheStatus::Ready);
    void* const unpinned = store(cache, 2, 100);

    store(cache, 3, 100);
    EXPECT_EQ(allocator.freed(), std::vector<void*>{unpinned});

    ASSERT_EQ(cache.begin_input(handle_of(3, 1, 100), view), CacheStatus::Ready);
    EXPECT_EQ(cache.pin_output(handle_of(4, 1, 100), view), CacheStatus::AllocationFailed);
    EXPECT_EQ(cache.bytes(), 200u);

    cache.unpin(GlobalBufferID{3});
    cache.unpin(oldest.id);
}

TEST(Unit_WorkerBufferCacheTest, GapInAPulledBufferDiscardsIt)
{
    FakeAllocator allocator;
    WorkerBufferCache cache(allocator.callables(), 1024);
    const BufferHandle handle = handle_of(9, 3, 12);
    const std::vector<std::byte> bytes = counting_bytes(12, 40);
    const std::chrono::milliseconds no_limit{0};
    BufferView view;

    ASSERT_EQ(cache.begin_input(handle, view), CacheStatus::NeedsPull);
    cache.accept_chunk(pull_chunk(handle, 0, 4, bytes), false);
    cache.accept_chunk(pull_chunk(handle, 8, 4, bytes), true);
    EXPECT_EQ(cache.wait_input(handle, no_limit, view), CacheStatus::Aborted);
    EXPECT_EQ(cache.bytes(), 0u) << "a discarded transfer kept its memory";

    ASSERT_EQ(cache.begin_input(handle, view), CacheStatus::NeedsPull);
    cache.accept_chunk(pull_chunk(handle, 0, 4, bytes), false);
    cache.accept_chunk(pull_chunk(handle, 4, 4, bytes), false);
    cache.accept_chunk(pull_chunk(handle, 8, 4, bytes), true);
    ASSERT_EQ(cache.wait_input(handle, no_limit, view), CacheStatus::Ready);
    ASSERT_EQ(view.size_bytes, bytes.size());
    EXPECT_EQ(std::memcmp(view.data, bytes.data(), bytes.size()), 0);
    cache.unpin(handle.id);
}

TEST(Unit_DispatchDeduplicatorTest, DuplicateDispatchRunsOnce)
{
    std::atomic<int> kernel_calls{0};
    Worker worker;
    const OperationID op(11);
    worker.kernels->register_kernel(
        op, KernelRegistry::Kernel([&kernel_calls](sycl::queue&, span<const BufferView>,
                                                   span<const BufferView>,
                                                   const NodeOptions&)
                                   {
                                       ++kernel_calls;
                                       return sycl::event{};
                                   }));
    worker.executor.start();

    DispatchDeduplicator seen;
    const auto deliver = [&worker, &seen, op](std::uint64_t task_id)
    {
        if (!seen.mark_seen(task_id))
            return;
        EXPECT_EQ(worker.executor.enqueue(K_MASTER, dispatch_of(task_id, op, {}, {}), 4096),
                  EnqueueStatus::Queued);
    };

    deliver(1);
    deliver(1);
    deliver(2);

    ASSERT_TRUE(worker.transport.wait_for(MessageType::TaskResult, 2));
    EXPECT_EQ(kernel_calls.load(), 2);
    EXPECT_EQ(worker.transport.count(MessageType::TaskAccepted), 2u);
    EXPECT_EQ(worker.result(0).task_id, 1u);
    EXPECT_EQ(worker.result(1).task_id, 2u);

    DispatchDeduplicator bounded(2);
    EXPECT_TRUE(bounded.mark_seen(1));
    EXPECT_TRUE(bounded.mark_seen(2));
    EXPECT_TRUE(bounded.mark_seen(3));
    EXPECT_EQ(bounded.size(), 2u);
    EXPECT_TRUE(bounded.mark_seen(1)) << "the oldest id was not forgotten at the capacity";
    EXPECT_FALSE(bounded.mark_seen(3));
}

TEST(Unit_DispatchExecutorTest, OutputAllocationFailureFailsTheTask)
{
    std::atomic<int> kernel_calls{0};
    Worker worker;
    const OperationID op(12);
    worker.kernels->register_kernel(
        op, KernelRegistry::Kernel([&kernel_calls](sycl::queue&, span<const BufferView>,
                                                   span<const BufferView>,
                                                   const NodeOptions&)
                                   {
                                       ++kernel_calls;
                                       return sycl::event{};
                                   }));
    worker.executor.start();

    worker.allocator.refuse(true);
    ASSERT_EQ(worker.executor.enqueue(K_MASTER, dispatch_of(1, op, {}, {handle_of(7, 1, 64)}),
                                      16),
              EnqueueStatus::Queued);
    ASSERT_TRUE(worker.transport.wait_for(MessageType::TaskResult, 1));
    EXPECT_EQ(worker.result(0).task_id, 1u);
    EXPECT_EQ(worker.result(0).status, Wire::TaskResultStatus::OutputAllocationFailed);
    EXPECT_EQ(kernel_calls.load(), 0);
    EXPECT_EQ(worker.transport.count(MessageType::BufferChunk), 0u);

    worker.allocator.refuse(false);
    ASSERT_EQ(worker.executor.enqueue(K_MASTER, dispatch_of(2, op, {}, {handle_of(7, 2, 64)}),
                                      16),
              EnqueueStatus::Queued);
    ASSERT_TRUE(worker.transport.wait_for(MessageType::TaskResult, 2));
    EXPECT_EQ(worker.result(1).status, Wire::TaskResultStatus::Succeeded);

    const WorkerStatistics statistics = worker.executor.statistics();
    EXPECT_EQ(statistics.tasks_failed_output_allocation, 1u);
    EXPECT_EQ(statistics.tasks_run, 1u);
}

TEST(Unit_DispatchExecutorTest, KernelSeesBufferSizes)
{
    std::atomic<std::size_t> input_size{0};
    std::atomic<std::size_t> output_size{0};
    Worker worker;
    const OperationID op(13);
    worker.kernels->register_kernel(
        op, KernelRegistry::Kernel(
                [&input_size, &output_size](sycl::queue&, span<const BufferView> inputs,
                                            span<const BufferView> outputs,
                                            const NodeOptions&)
                {
                    input_size = inputs[0].size_bytes;
                    output_size = outputs[0].size_bytes;
                    std::memset(outputs[0].data, 0, outputs[0].size_bytes);
                    std::memcpy(outputs[0].data, inputs[0].data, inputs[0].size_bytes);
                    return sycl::event{};
                }));
    worker.executor.start();

    const BufferHandle input = handle_of(3, 5, 12);
    const BufferHandle output = handle_of(4, 1, 20);
    const std::vector<std::byte> bytes = counting_bytes(12, 1);
    ASSERT_EQ(worker.executor.enqueue(K_MASTER, dispatch_of(1, op, {input}, {output}), 8),
              EnqueueStatus::Queued);

    ASSERT_TRUE(worker.transport.wait_for(MessageType::BufferRequest, 1));
    const std::vector<SentFrame> requests =
        worker.transport.frames_of(MessageType::BufferRequest);
    const Wire::BufferRequestMessage request =
        decode<Wire::BufferRequestMessage>(requests[0]);
    EXPECT_EQ(request.id, input.id);
    EXPECT_EQ(request.version, input.version);

    worker.cache.accept_chunk(pull_chunk(input, 0, 8, bytes), false);
    worker.cache.accept_chunk(pull_chunk(input, 8, 4, bytes), true);

    ASSERT_TRUE(worker.transport.wait_for(MessageType::TaskResult, 1));
    EXPECT_EQ(worker.result(0).status, Wire::TaskResultStatus::Succeeded);
    EXPECT_EQ(input_size.load(), 12u);
    EXPECT_EQ(output_size.load(), 20u);

    const std::vector<SentFrame> chunks = worker.transport.frames_of(MessageType::BufferChunk);
    ASSERT_EQ(chunks.size(), 3u);
    const Wire::BufferChunkMessage first = decode<Wire::BufferChunkMessage>(chunks[0]);
    EXPECT_EQ(first.purpose, Wire::ChunkPurpose::TaskOutput);
    EXPECT_EQ(first.handle.size_bytes, 20u);
    ASSERT_EQ(first.data.size(), 8u);
    EXPECT_EQ(std::memcmp(first.data.data(), bytes.data(), 8), 0);
}

TEST(Unit_DispatchExecutorTest, GenericLambdaRegistersAsAKernel)
{
    Worker worker;
    const OperationID op(14);
    worker.kernels->register_kernel(
        op, [](auto& /*queue*/, auto /*inputs*/, auto outputs, const auto& /*metadata*/)
            {
                std::memset(outputs[0].data, 0x5a, outputs[0].size_bytes);
                return sycl::event{};
            });
    worker.executor.start();

    ASSERT_EQ(worker.executor.enqueue(K_MASTER, dispatch_of(1, op, {}, {handle_of(8, 1, 4)}),
                                      4096),
              EnqueueStatus::Queued);
    ASSERT_TRUE(worker.transport.wait_for(MessageType::TaskResult, 1));
    EXPECT_EQ(worker.result(0).status, Wire::TaskResultStatus::Succeeded);

    const std::vector<SentFrame> chunks = worker.transport.frames_of(MessageType::BufferChunk);
    ASSERT_EQ(chunks.size(), 1u);
    const Wire::BufferChunkMessage chunk = decode<Wire::BufferChunkMessage>(chunks[0]);
    ASSERT_EQ(chunk.data.size(), 4u);
    for (const std::byte byte : chunk.data)
        EXPECT_EQ(byte, std::byte{0x5a});
}

TEST(Unit_DispatchExecutorTest, KernelThatThrowsFailsTheTaskAndTheWorkerLives)
{
    std::atomic<int> kernel_calls{0};
    Worker worker;
    const OperationID op(15);
    worker.kernels->register_kernel(
        op, KernelRegistry::Kernel([&kernel_calls](sycl::queue&, span<const BufferView>,
                                                   span<const BufferView>,
                                                   const NodeOptions&)
                                   {
                                       if (++kernel_calls == 1)
                                           throw std::runtime_error("kernel refused");
                                       return sycl::event{};
                                   }));
    worker.executor.start();

    ASSERT_EQ(worker.executor.enqueue(K_MASTER, dispatch_of(1, op, {}, {handle_of(5, 1, 16)}),
                                      4096),
              EnqueueStatus::Queued);
    ASSERT_TRUE(worker.transport.wait_for(MessageType::TaskResult, 1));
    EXPECT_EQ(worker.result(0).status, Wire::TaskResultStatus::KernelFailed);
    EXPECT_TRUE(worker.result(0).outputs.empty());
    EXPECT_EQ(worker.transport.count(MessageType::BufferChunk), 0u);

    ASSERT_EQ(worker.executor.enqueue(K_MASTER, dispatch_of(2, op, {}, {handle_of(5, 2, 16)}),
                                      4096),
              EnqueueStatus::Queued);
    ASSERT_TRUE(worker.transport.wait_for(MessageType::TaskResult, 2));
    EXPECT_EQ(worker.result(1).status, Wire::TaskResultStatus::Succeeded);

    const WorkerStatistics statistics = worker.executor.statistics();
    EXPECT_EQ(statistics.tasks_failed_kernel, 1u);
    EXPECT_EQ(statistics.tasks_run, 1u);
}

TEST(Unit_DispatchExecutorTest, OutputStreamEndsWithTheResult)
{
    Worker worker;
    const OperationID op(16);
    const std::vector<std::byte> bytes = counting_bytes(10, 100);
    worker.kernels->register_kernel(
        op, KernelRegistry::Kernel([&bytes](sycl::queue&, span<const BufferView>,
                                            span<const BufferView> outputs,
                                            const NodeOptions&)
                                   {
                                       std::memcpy(outputs[0].data, bytes.data(), bytes.size());
                                       return sycl::event{};
                                   }));
    worker.transport.keep_streams();
    worker.executor.start();

    const BufferHandle output = handle_of(5, 2, 10);
    ASSERT_EQ(worker.executor.enqueue(K_MASTER, dispatch_of(9, op, {}, {output}), 4),
              EnqueueStatus::Queued);
    ASSERT_TRUE(worker.transport.wait_for_streams(1));

    EXPECT_EQ(worker.transport.count(MessageType::TaskResult), 0u);
    EXPECT_EQ(worker.executor.oldest_held_task_id(), 9u);
    worker.cache.release(output.id);
    EXPECT_EQ(worker.cache.bytes(), 10u) << "the stream's pin did not keep its output";

    worker.transport.release_streams();

    const std::vector<SentFrame> frames = worker.transport.frames();
    ASSERT_EQ(frames.size(), 5u);
    EXPECT_EQ(frames[0].type, MessageType::TaskAccepted);
    const std::size_t lengths[] = {4, 4, 2};
    std::uint64_t offset = 0;
    for (std::size_t index = 0; index < 3; ++index)
    {
        const SentFrame& frame = frames[index + 1];
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
    const Wire::TaskResultMessage result = decode<Wire::TaskResultMessage>(frames[4]);
    EXPECT_EQ(result.status, Wire::TaskResultStatus::Succeeded);
    ASSERT_EQ(result.outputs.size(), 1u);
    EXPECT_EQ(result.outputs[0].id, output.id);

    EXPECT_EQ(worker.executor.oldest_held_task_id(), 10u);
    EXPECT_EQ(worker.cache.bytes(), 0u) << "the stream's destructor kept a pin";
}

TEST(Unit_DispatchExecutorTest, DisconnectEndsAnOpenInputPull)
{
    // With no idle timeout the pull ends only when something ends it.
    DispatchExecutorOptions options;
    options.pull_idle_timeout = std::chrono::milliseconds(0);
    Worker worker(1024 * 1024, options);

    std::atomic<int> kernel_calls{0};
    const OperationID op(18);
    worker.kernels->register_kernel(
        op, KernelRegistry::Kernel([&kernel_calls](sycl::queue&, span<const BufferView>,
                                                   span<const BufferView>,
                                                   const NodeOptions&)
                                   {
                                       ++kernel_calls;
                                       return sycl::event{};
                                   }));
    worker.executor.start();

    const BufferHandle input = handle_of(3, 5, 12);
    ASSERT_EQ(worker.executor.enqueue(K_MASTER, dispatch_of(1, op, {input}, {}), 8),
              EnqueueStatus::Queued);
    ASSERT_TRUE(worker.transport.wait_for(MessageType::BufferRequest, 1));

    worker.executor.discard_queued();

    ASSERT_TRUE(worker.transport.wait_for(MessageType::TaskResult, 1))
        << "the exec thread still waits for an input no master will send";
    EXPECT_EQ(worker.result(0).status, Wire::TaskResultStatus::InputUnavailable);
    EXPECT_EQ(kernel_calls.load(), 0);
    EXPECT_EQ(worker.executor.statistics().tasks_failed_input_unavailable, 1u);
    EXPECT_EQ(worker.cache.bytes(), 0u) << "the abandoned pull kept its allocation";

    // The exec thread is free again: a task of the next connection runs.
    ASSERT_EQ(worker.executor.enqueue(PeerID{2}, dispatch_of(1, op, {}, {}), 8),
              EnqueueStatus::Queued);
    ASSERT_TRUE(worker.transport.wait_for(MessageType::TaskResult, 2));
    EXPECT_EQ(worker.result(1).status, Wire::TaskResultStatus::Succeeded);
    EXPECT_EQ(kernel_calls.load(), 1);
}

TEST(Unit_MasterLinkTest, HandshakeChecksTheMasterBeforeProving)
{
    FakeTransport transport;
    const std::vector<std::uint8_t> secret = secret_of(0x42);
    const span<const std::uint8_t> key(secret.data(), secret.size());
    MasterLink link(transport, key, link_options(std::chrono::milliseconds(1000)),
                    fixed_nonces());
    const PeerID peer{1};

    link.on_connected(peer);
    const std::vector<SentFrame> hellos = transport.frames_of(MessageType::Hello);
    ASSERT_EQ(hellos.size(), 1u);
    const Wire::HelloMessage hello = decode<Wire::HelloMessage>(hellos[0]);
    EXPECT_EQ(std::memcmp(hello.worker_nonce.data(), K_WORKER_NONCE.bytes.data(),
                          hello.worker_nonce.size()),
              0);
    EXPECT_EQ(hello.node_name, "worker-a");
    EXPECT_EQ(hello.build_id, "test-build");
    EXPECT_EQ(transport.count(MessageType::Proof), 0u) << "PROOF left before the CHALLENGE";

    EXPECT_EQ(link.on_challenge(peer, challenge_of(secret, 7)), LinkChange::None);
    const std::vector<SentFrame> proofs = transport.frames_of(MessageType::Proof);
    ASSERT_EQ(proofs.size(), 1u);
    const Wire::ProofMessage proof = decode<Wire::ProofMessage>(proofs[0]);
    const Sha256Digest expected =
        compute_proof(HandshakeRole::Worker, key, K_WORKER_NONCE, K_MASTER_NONCE);
    EXPECT_EQ(std::memcmp(proof.worker_proof.data(), expected.data(), expected.size()), 0);
    EXPECT_EQ(proof.ops.size(), 2u);
    EXPECT_EQ(proof.max_jobs, 16u);
    EXPECT_EQ(proof.cache_budget, 4096u);
    EXPECT_FALSE(link.master().has_value());

    Wire::WelcomeMessage welcome;
    welcome.worker_id = 1;
    welcome.liveness_timeout_ms = 5000;
    welcome.ping_interval_ms = 250;
    welcome.chunk_bytes = 4096;
    EXPECT_EQ(link.on_welcome(peer, welcome), LinkChange::RegisteredNewSession);
    ASSERT_TRUE(link.master().has_value());
    EXPECT_EQ(*link.master(), peer);
    EXPECT_EQ(link.chunk_bytes(), 4096u);
    EXPECT_EQ(link.end(), WorkerEnd::Running);
}

TEST(Unit_MasterLinkTest, WrongMasterProofEndsTheLink)
{
    FakeTransport transport;
    const std::vector<std::uint8_t> secret = secret_of(0x42);
    MasterLink link(transport, span<const std::uint8_t>(secret.data(), secret.size()),
                    link_options(std::chrono::milliseconds(1000)), fixed_nonces());
    const PeerID peer{1};

    link.on_connected(peer);
    EXPECT_EQ(link.on_challenge(peer, challenge_of(secret_of(0x43), 7)), LinkChange::Ended);
    EXPECT_EQ(link.end(), WorkerEnd::RejectedAuthentication);
    EXPECT_EQ(transport.count(MessageType::Proof), 0u) << "PROOF went to an unproven master";
    EXPECT_TRUE(transport.is_closed(peer));

    const PeerID next{2};
    link.on_connected(next);
    EXPECT_EQ(transport.count(MessageType::Hello), 1u) << "an ended link sent HELLO";
    EXPECT_TRUE(transport.is_closed(next));
}

TEST(Unit_MasterLinkTest, EndedLinkStopsItsTransport)
{
    FakeTransport transport;
    const std::vector<std::uint8_t> secret = secret_of(0x42);
    MasterLink link(transport, span<const std::uint8_t>(secret.data(), secret.size()),
                    link_options(std::chrono::milliseconds(60000)), fixed_nonces());
    const PeerID peer{1};
    link.start();

    link.on_connected(peer);
    EXPECT_EQ(transport.stop_count(), 0u) << "a running link stopped its transport";
    ASSERT_EQ(link.on_challenge(peer, challenge_of(secret_of(0x43), 7)), LinkChange::Ended);

    ASSERT_TRUE(transport.wait_for_stop())
        << "the transport of a rejected link keeps connecting";
    link.stop();
    EXPECT_EQ(transport.stop_count(), 1u);
    EXPECT_EQ(link.end(), WorkerEnd::RejectedAuthentication);
}

TEST(Unit_MasterLinkTest, PingGetsNoPongBeforeRegistration)
{
    FakeTransport transport;
    const std::vector<std::uint8_t> secret = secret_of(0x42);
    MasterLink link(transport, span<const std::uint8_t>(secret.data(), secret.size()),
                    link_options(std::chrono::milliseconds(1000)), fixed_nonces());
    const PeerID peer{1};
    Wire::PingMessage ping;
    ping.token = 99;

    // The master registered this worker on PROOF, but its WELCOME has not arrived.
    link.on_connected(peer);
    ASSERT_EQ(link.on_challenge(peer, challenge_of(secret, 7)), LinkChange::None);
    link.on_ping(peer, ping);
    EXPECT_EQ(transport.count(MessageType::Pong), 0u)
        << "a link that is not registered told the master it is alive";

    Wire::WelcomeMessage welcome;
    welcome.worker_id = peer.value;
    welcome.chunk_bytes = 4096;
    ASSERT_EQ(link.on_welcome(peer, welcome), LinkChange::RegisteredNewSession);
    link.on_ping(peer, ping);

    const std::vector<SentFrame> pongs = transport.frames_of(MessageType::Pong);
    ASSERT_EQ(pongs.size(), 1u);
    EXPECT_EQ(decode<Wire::PongMessage>(pongs[0]).token, 99u);
}

TEST(Unit_MasterLinkTest, LateChallengeForAForgottenHelloIsIgnored)
{
    constexpr std::size_t K_HELLOS = 5;
    FakeTransport transport;
    const std::vector<std::uint8_t> secret = secret_of(0x42);
    std::atomic<std::uint8_t> drawn{0};
    MasterLink link(transport, span<const std::uint8_t>(secret.data(), secret.size()),
                    link_options(std::chrono::milliseconds(1)),
                    [&drawn] { return nonce_of(++drawn); });
    const PeerID peer{1};
    link.start();

    // The link remembers four nonces; the fifth HELLO forgets the nonce of the first.
    link.on_connected(peer);
    ASSERT_TRUE(transport.wait_for(MessageType::Hello, K_HELLOS));
    link.stop();

    EXPECT_EQ(link.on_challenge(peer, challenge_of(secret, 7, nonce_of(1))), LinkChange::None)
        << "a CHALLENGE for a forgotten HELLO was taken for a wrong secret";
    EXPECT_EQ(transport.count(MessageType::Proof), 0u);
    EXPECT_FALSE(transport.is_closed(peer));
}

TEST(Unit_MasterLinkTest, IdleHeartbeatCarriesTheNextTaskID)
{
    Worker worker;
    const OperationID op(17);
    worker.kernels->register_kernel(
        op, KernelRegistry::Kernel([](sycl::queue&, span<const BufferView>,
                                      span<const BufferView>, const NodeOptions&)
                                   {
                                       return sycl::event{};
                                   }));
    worker.executor.start();
    EXPECT_EQ(worker.executor.oldest_held_task_id(), 0u);

    ASSERT_EQ(worker.executor.enqueue(K_MASTER, dispatch_of(41, op, {}, {}), 4096),
              EnqueueStatus::Queued);
    ASSERT_TRUE(worker.transport.wait_for(MessageType::TaskResult, 1));

    const std::vector<std::uint8_t> secret = secret_of(0x42);
    MasterLinkOptions options = link_options(std::chrono::milliseconds(5));
    options.heartbeat_probe = [&worker]
    {
        Wire::HeartbeatMessage heartbeat;
        heartbeat.queue_depth = worker.executor.queue_depth();
        heartbeat.oldest_held_task_id = worker.executor.oldest_held_task_id();
        heartbeat.cache_bytes = worker.cache.bytes();
        return heartbeat;
    };
    MasterLink link(worker.transport, span<const std::uint8_t>(secret.data(), secret.size()),
                    std::move(options), fixed_nonces());
    link.start();
    ASSERT_EQ(register_link(link, K_MASTER, secret, 7, 0), LinkChange::RegisteredNewSession);

    ASSERT_TRUE(worker.transport.wait_for(MessageType::Heartbeat, 1));
    const std::vector<SentFrame> heartbeats = worker.transport.frames_of(MessageType::Heartbeat);
    const Wire::HeartbeatMessage heartbeat = decode<Wire::HeartbeatMessage>(heartbeats[0]);
    EXPECT_EQ(heartbeat.oldest_held_task_id, 42u);
    EXPECT_EQ(heartbeat.queue_depth, 0u);
    link.stop();
    EXPECT_EQ(link.end(), WorkerEnd::Stopped);
}

TEST(Unit_MasterLinkTest, SilentMasterIsLost)
{
    FakeTransport transport;
    const std::vector<std::uint8_t> secret = secret_of(0x42);
    MasterLink link(transport, span<const std::uint8_t>(secret.data(), secret.size()),
                    link_options(std::chrono::milliseconds(60000)), fixed_nonces());
    const PeerID peer{1};
    link.start();

    ASSERT_EQ(register_link(link, peer, secret, 7, 40), LinkChange::RegisteredNewSession);
    ASSERT_TRUE(transport.wait_for_close(peer)) << "a silent master was never closed";
    EXPECT_FALSE(link.master().has_value());

    PeerLoss loss;
    loss.cause = PeerLossCause::ClosedLocally;
    EXPECT_EQ(link.on_lost(peer, loss), LinkChange::Disconnected);
    EXPECT_EQ(link.end(), WorkerEnd::Running) << "a lost master ended a link that may reconnect";
    link.stop();
}

TEST(Unit_MasterLinkTest, NewSessionClearsTheCache)
{
    FakeTransport transport;
    FakeAllocator allocator;
    WorkerBufferCache cache(allocator.callables(), 1024);
    DispatchDeduplicator seen;
    const std::vector<std::uint8_t> secret = secret_of(0x42);
    MasterLink link(transport, span<const std::uint8_t>(secret.data(), secret.size()),
                    link_options(std::chrono::milliseconds(1000)), fixed_nonces());

    const auto apply = [&cache, &seen](LinkChange change)
    {
        if (change != LinkChange::RegisteredNewSession)
            return;
        cache.clear();
        seen.clear();
    };
    PeerLoss loss;
    loss.cause = PeerLossCause::ClosedByPeer;

    LinkChange change = register_link(link, PeerID{1}, secret, 7, 0);
    EXPECT_EQ(change, LinkChange::RegisteredNewSession);
    apply(change);
    store(cache, 1, 32);
    EXPECT_TRUE(seen.mark_seen(5));

    EXPECT_EQ(link.on_lost(PeerID{1}, loss), LinkChange::Disconnected);
    change = register_link(link, PeerID{2}, secret, 7, 0);
    EXPECT_EQ(change, LinkChange::Registered);
    apply(change);
    EXPECT_EQ(cache.bytes(), 32u) << "the cache was cleared for the session it was filled in";
    EXPECT_EQ(seen.size(), 1u);

    EXPECT_EQ(link.on_lost(PeerID{2}, loss), LinkChange::Disconnected);
    change = register_link(link, PeerID{3}, secret, 8, 0);
    EXPECT_EQ(change, LinkChange::RegisteredNewSession);
    apply(change);
    EXPECT_EQ(cache.bytes(), 0u);
    EXPECT_EQ(seen.size(), 0u);
    EXPECT_EQ(allocator.live(), 0);
    EXPECT_EQ(link.reconnects(), 2u);
}

TEST(Unit_MasterLinkTest, LossAfterGoodbyeIsTheWorkersOwn)
{
    FakeTransport transport;
    const std::vector<std::uint8_t> secret = secret_of(0x42);
    MasterLink link(transport, span<const std::uint8_t>(secret.data(), secret.size()),
                    link_options(std::chrono::milliseconds(60000)), fixed_nonces());
    const PeerID peer{1};

    ASSERT_EQ(register_link(link, peer, secret, 7, 0), LinkChange::RegisteredNewSession);
    link.say_goodbye();
    EXPECT_EQ(transport.count(MessageType::Goodbye), 1u);

    PeerLoss loss;
    loss.cause = PeerLossCause::ClosedLocally;
    EXPECT_EQ(link.on_lost(peer, loss), LinkChange::None)
        << "the worker's own close was reported as a lost master";
    EXPECT_FALSE(link.master().has_value());

    EXPECT_EQ(link.on_connect_abandoned(), LinkChange::None);
    EXPECT_EQ(link.end(), WorkerEnd::Running) << "a leaving worker ended as MasterLost";

    const PeerID next{2};
    link.on_connected(next);
    EXPECT_EQ(transport.count(MessageType::Hello), 1u) << "a leaving link sent HELLO";
    EXPECT_TRUE(transport.is_closed(next));

    link.stop();
    EXPECT_EQ(link.end(), WorkerEnd::Stopped);
}

namespace
{
    /** @brief Returns a kernel that submits nothing and returns a completed event. */
    KernelRegistry::Kernel idle_kernel()
    {
        return [](sycl::queue&, span<const BufferView>, span<const BufferView>,
                  const NodeOptions&) { return sycl::event{}; };
    }

} // namespace

/** @brief Tests that a sealed registry keeps its kernels and stops a registration. */
TEST(Unit_KernelRegistryDeathTest, SealedRegistryStopsARegistration)
{
    const OperationID early(21);
    const OperationID late(22);
    const sushi_ptr<KernelRegistry> kernels = make_sushi<KernelRegistry>();
    kernels->register_kernel(early, idle_kernel());
    EXPECT_FALSE(kernels->is_sealed());

    kernels->seal();
    kernels->seal();

    EXPECT_TRUE(kernels->is_sealed());
    EXPECT_DEATH(kernels->register_kernel(late, idle_kernel()), "the registry is sealed");
    EXPECT_DEATH(kernels->register_kernel(early, idle_kernel()), "the registry is sealed");
    EXPECT_TRUE(kernels->has(early));
    EXPECT_FALSE(kernels->has(late));
    EXPECT_EQ(kernels->known_ops().size(), 1u);
}

/** @brief Tests that a kernel that holds no callable stops the process, sealed or not. */
TEST(Unit_KernelRegistryDeathTest, EmptyKernelStopsTheRegistration)
{
    const OperationID operation(23);
    const sushi_ptr<KernelRegistry> kernels = make_sushi<KernelRegistry>();

    EXPECT_DEATH(kernels->register_kernel(operation, KernelRegistry::Kernel()),
                 "the kernel of operation 23 is empty");

    // An empty kernel is named as the fault before the seal is.
    kernels->seal();
    EXPECT_DEATH(kernels->register_kernel(operation, KernelRegistry::Kernel()),
                 "the kernel of operation 23 is empty");
}
