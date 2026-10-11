/**************************************************************************/
/* test_distributed.cpp                                                   */
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
 * @file test_distributed.cpp
 * @brief Tests codec round trips and fuzz, the in-process transport and the offload policy.
 * @see modules/extension/distributed/tests/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/distributed/buffer_handle.hpp>
#include <SushiRuntime/distributed/offload_policy.hpp>
#include <SushiRuntime/distributed/transport.hpp>
#include <SushiRuntime/distributed/in_process_transport.hpp>
#include <SushiRuntime/distributed/message_type.hpp>

#include "wire_codec.hpp"
#include "wire_frame.hpp"
#include "wire_messages.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;
using namespace SushiRuntime::Distributed::Wire;

namespace
{
    /** @brief Names the bytes of a payload or a frame. */
    using Bytes = std::vector<std::byte>;

    /** @brief Holds the seed of every random source in this file. */
    constexpr std::uint64_t K_SEED = 20261010u;

    /** @brief Holds the number of random dispatches the round trip encodes. */
    constexpr int K_ROUND_TRIPS = 200;

    /** @brief Holds the number of mutations the fuzz applies to each sample payload. */
    constexpr int K_MUTATIONS_PER_SAMPLE = 500;

    /** @brief Holds the number of random headers the header fuzz decodes. */
    constexpr int K_RANDOM_HEADERS = 4000;

    /** @brief Holds how long a transport test waits for an event. */
    constexpr std::chrono::seconds K_EVENT_LIMIT{10};

    /** @brief Returns a read-only view of a byte vector. */
    span<const std::byte> view_of(const Bytes& bytes)
    {
        return span<const std::byte>(bytes.data(), bytes.size());
    }

    /** @brief Returns a random handle; every field takes any value of its width. */
    BufferHandle random_handle(std::mt19937_64& random)
    {
        BufferHandle handle;
        handle.id.value = random();
        handle.version = static_cast<std::uint32_t>(random());
        handle.size_bytes = random();
        return handle;
    }

    /** @brief Returns a list of up to @p most random handles. */
    std::vector<BufferHandle> random_handles(std::mt19937_64& random, std::size_t most)
    {
        std::vector<BufferHandle> handles(random() % (most + 1));
        for (BufferHandle& handle : handles)
            handle = random_handle(random);
        return handles;
    }

    /** @brief Returns a dispatch whose every wire field is drawn from @p random. */
    TaskDispatchMessage random_dispatch(std::mt19937_64& random)
    {
        const auto first_type = static_cast<std::uint64_t>(Internal::Graph::TaskType::None);
        const auto last_type = static_cast<std::uint64_t>(Internal::Graph::TaskType::ControlFlow);

        TaskDispatchMessage message;
        message.task_id = random();
        message.metadata.name = "never_on_the_wire";
        message.metadata.op_id = OperationID(static_cast<std::uint32_t>(random()));
        message.metadata.task_type = static_cast<Internal::Graph::TaskType>(
            first_type + random() % (last_type - first_type + 1));
        message.metadata.priority = static_cast<std::int32_t>(random());
        message.metadata.numa_affinity = static_cast<std::int32_t>(random());

        // A parameter is zero one time in four, so the trailing-zero cut is exercised.
        for (std::uint64_t& parameter : message.metadata.params)
            parameter = random() % 4 == 0 ? 0 : random();

        message.inputs = random_handles(random, 6);
        message.outputs = random_handles(random, 4);
        return message;
    }

    /** @brief Expects two handle lists to hold the same handles in the same order. */
    void expect_same_handles(const std::vector<BufferHandle>& actual,
                             const std::vector<BufferHandle>& expected)
    {
        ASSERT_EQ(actual.size(), expected.size());
        for (std::size_t index = 0; index < expected.size(); ++index)
        {
            EXPECT_EQ(actual[index].id, expected[index].id) << "handle " << index;
            EXPECT_EQ(actual[index].version, expected[index].version) << "handle " << index;
            EXPECT_EQ(actual[index].size_bytes, expected[index].size_bytes)
                << "handle " << index;
        }
    }

    /** @brief Decodes a payload as @p Message and returns only the outcome. */
    template <typename Message>
    DecodeResult decode_as(span<const std::byte> payload)
    {
        Message message;
        return Message::decode(payload, message);
    }

    /** @brief Holds one well-formed payload and the decoder of its message. */
    struct Sample
    {
        /** @brief The message the payload encodes. */
        MessageType type = MessageType::Hello;

        /** @brief The encoded payload. */
        Bytes payload;

        /** @brief The decoder of the message. */
        DecodeResult (*decode)(span<const std::byte>) = nullptr;
    };

    /** @brief Returns the sample of one message; a message that does not encode fails. */
    template <typename Message>
    Sample sample_of(const Message& message)
    {
        Sample sample;
        sample.type = Message::K_TYPE;
        sample.decode = &decode_as<Message>;
        EXPECT_EQ(message.encode(sample.payload), CodecStatus::Ok)
            << message_type_name(Message::K_TYPE);
        return sample;
    }

    /** @brief Returns one well-formed payload for each of the sixteen messages. */
    std::vector<Sample> every_sample()
    {
        std::mt19937_64 random(K_SEED);
        const Bytes chunk_data(48, std::byte{0x5A});

        HelloMessage hello;
        hello.build_id = "build-1";
        hello.node_name = "worker-a";

        ProofMessage proof;
        proof.ops = {OperationID(7), OperationID(8), OperationID(9)};
        proof.max_jobs = 16;
        proof.cache_budget = 1u << 20;

        RejectMessage reject;
        reject.reason = RejectReason::Authentication;
        reject.sender_version = K_PROTOCOL_VERSION;
        reject.text = "the proof does not match";

        GoodbyeMessage goodbye;
        goodbye.text = "worker stopping";

        TaskResultMessage result;
        result.task_id = 11;
        result.outputs = random_handles(random, 3);

        BufferChunkMessage chunk;
        chunk.purpose = ChunkPurpose::TaskOutput;
        chunk.task_id = 11;
        chunk.handle = BufferHandle{GlobalBufferID{3}, 2, chunk_data.size()};
        chunk.data = view_of(chunk_data);

        std::vector<Sample> samples;
        samples.push_back(sample_of(hello));
        samples.push_back(sample_of(ChallengeMessage()));
        samples.push_back(sample_of(proof));
        samples.push_back(sample_of(WelcomeMessage()));
        samples.push_back(sample_of(reject));
        samples.push_back(sample_of(goodbye));
        samples.push_back(sample_of(random_dispatch(random)));
        samples.push_back(sample_of(TaskAcceptedMessage()));
        samples.push_back(sample_of(result));
        samples.push_back(sample_of(BufferRequestMessage()));
        samples.push_back(sample_of(chunk));
        samples.push_back(sample_of(BufferAbortMessage()));
        samples.push_back(sample_of(BufferReleaseMessage()));
        samples.push_back(sample_of(PingMessage()));
        samples.push_back(sample_of(PongMessage()));
        samples.push_back(sample_of(HeartbeatMessage()));
        return samples;
    }

    /** @brief Returns a random byte. */
    std::byte random_byte(std::mt19937_64& random)
    {
        return static_cast<std::byte>(random() & 0xFFu);
    }

    /**
     * @brief Returns a payload damaged in one of four ways: a byte replaced, the tail cut,
     *        bytes appended, or four bytes in a row set to 0xFF.
     */
    Bytes mutated(const Bytes& payload, std::mt19937_64& random)
    {
        Bytes damaged = payload;
        const std::uint64_t way = random() % 4;
        if (way == 0 && !damaged.empty())
        {
            damaged[random() % damaged.size()] = random_byte(random);
        }
        else if (way == 1)
        {
            damaged.resize(random() % (damaged.size() + 1));
        }
        else if (way == 2)
        {
            const std::size_t extra = 1 + random() % 8;
            for (std::size_t index = 0; index < extra; ++index)
                damaged.push_back(random_byte(random));
        }
        else if (!damaged.empty())
        {
            const std::size_t first = random() % damaged.size();
            const std::size_t end = std::min(damaged.size(), first + 4);
            std::fill(damaged.begin() + static_cast<std::ptrdiff_t>(first),
                      damaged.begin() + static_cast<std::ptrdiff_t>(end), std::byte{0xFF});
        }
        return damaged;
    }

    /** @brief Holds one frame a transport delivered. */
    struct ReceivedFrame
    {
        /** @brief The peer the frame came from. */
        PeerID peer;

        /** @brief The type the header carried. */
        MessageType type = MessageType::Hello;

        /** @brief The flag byte the header carried. */
        std::uint8_t flags = 0;

        /** @brief A copy of the payload. */
        Bytes payload;
    };

    /**
     * @brief Records the connections and the frames of one endpoint and wakes a waiting test.
     */
    class RecordingEvents final : public TransportEvents
    {
        public:
            /** @brief Records the peer of a connection that opened. */
            void on_peer_connected(PeerID peer, const PeerDescription& /*info*/) override
            {
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    connected_.push_back(peer);
                }
                changed_.notify_all();
            }

            /** @brief Records a copy of a frame. */
            void on_frame(PeerID peer, MessageType type, std::uint8_t flags,
                          span<const std::byte> payload) override
            {
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    frames_.push_back(
                        ReceivedFrame{peer, type, flags, Bytes(payload.begin(), payload.end())});
                }
                changed_.notify_all();
            }

            /** @brief Ignores a loss; the test stops both endpoints itself. */
            void on_peer_lost(PeerID /*peer*/, const PeerLoss& /*loss*/) override
            {

            }

            /** @brief Ignores an abandoned connect; the test sets no connect timeout. */
            void on_connect_abandoned(const std::string& /*cause*/) override
            {

            }

            /** @brief Waits for the first connection and returns its peer, or nothing. */
            [[nodiscard]] std::optional<PeerID> wait_for_peer()
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (!changed_.wait_for(lock, K_EVENT_LIMIT, [&] { return !connected_.empty(); }))
                    return std::nullopt;
                return connected_.front();
            }

            /** @brief Waits for the first frame and returns it, or nothing. */
            [[nodiscard]] std::optional<ReceivedFrame> wait_for_frame()
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (!changed_.wait_for(lock, K_EVENT_LIMIT, [&] { return !frames_.empty(); }))
                    return std::nullopt;
                return frames_.front();
            }

        private:
            /// @brief Guards the two lists.
            std::mutex mutex_;

            /// @brief Wakes a waiting test on every event.
            std::condition_variable changed_;

            /// @brief The peers that connected, in order.
            std::vector<PeerID> connected_;

            /// @brief The frames that arrived, in order.
            std::vector<ReceivedFrame> frames_;
    };

    /** @brief Holds a payload size above the default 64 KiB gate of the policy: 1 MiB. */
    constexpr std::uint64_t K_LARGE_PAYLOAD = 1u << 20;

    /** @brief Holds a free-memory figure no test payload exhausts: 1 GiB. */
    constexpr std::uint64_t K_AMPLE_MEMORY = 1ull << 30;

    /** @brief Returns the statistics of a live worker as a policy sees them. */
    WorkerLoad live_worker(int id, std::uint32_t inflight, std::uint64_t free_memory,
                            std::uint32_t round_trip_micros)
    {
        WorkerLoad worker;
        worker.worker_id = id;
        worker.alive = true;
        worker.inflight = inflight;
        worker.free_mem = free_memory;
        worker.rtt_micros = round_trip_micros;
        return worker;
    }

    /** @brief Asks the default policy to place a task of @p bytes on one of @p workers. */
    std::optional<int> pick(const std::vector<WorkerLoad>& workers, std::uint64_t bytes)
    {
        LeastLoadedOffloadPolicy policy;
        const sushi_ptr<Internal::Graph::Node> node = make_sushi<Internal::Graph::Node>();
        return policy.pick_worker(
            *node, span<const WorkerLoad>(workers.data(), workers.size()), bytes);
    }
} // namespace

/** @brief Tests that random dispatches cross a whole frame with every wire field intact. */
TEST(Unit_DistributedTest, SeededDispatchesSurviveAWholeFrame)
{
    std::mt19937_64 random(K_SEED);
    Bytes frame;
    for (int round = 0; round < K_ROUND_TRIPS; ++round)
    {
        SCOPED_TRACE(round);
        const TaskDispatchMessage sent = random_dispatch(random);

        frame.clear();
        ASSERT_EQ(append_frame(sent, 0, frame), CodecStatus::Ok);

        FrameHeader header;
        ASSERT_EQ(decode_frame_header(view_of(frame), header), FrameStatus::Ok);
        EXPECT_EQ(header.type, MessageType::TaskDispatch);
        EXPECT_EQ(header.flags, 0u);
        ASSERT_EQ(header.payload_length, frame.size() - K_FRAME_HEADER_BYTES);

        TaskDispatchMessage received;
        const DecodeResult decoded = TaskDispatchMessage::decode(
            view_of(frame).subspan(K_FRAME_HEADER_BYTES, header.payload_length), received);
        ASSERT_TRUE(decoded.ok()) << "offset " << decoded.offset;

        EXPECT_EQ(received.task_id, sent.task_id);
        EXPECT_EQ(received.metadata.op_id.value, sent.metadata.op_id.value);
        EXPECT_EQ(received.metadata.task_type, sent.metadata.task_type);
        EXPECT_EQ(received.metadata.priority, sent.metadata.priority);
        EXPECT_EQ(received.metadata.numa_affinity, sent.metadata.numa_affinity);
        for (std::size_t index = 0; index < Internal::Graph::TaskMetadata::K_MAX_PARAMS; ++index)
            EXPECT_EQ(received.metadata.params[index], sent.metadata.params[index])
                << "parameter " << index;
        EXPECT_STRNE(received.metadata.name, sent.metadata.name);
        expect_same_handles(received.inputs, sent.inputs);
        expect_same_handles(received.outputs, sent.outputs);
    }
}

/** @brief Tests that frames of both directions arrive with the sender's peer id and type. */
TEST(Unit_DistributedTest, InProcessTransportDeliversBothDirections)
{
    RecordingEvents master_events;
    RecordingEvents worker_events;
    InProcessFabric fabric;
    const std::shared_ptr<InProcessTransport> master = InProcessTransport::make_master(fabric);
    const std::shared_ptr<InProcessTransport> worker = InProcessTransport::make_worker(fabric);
    EXPECT_EQ(fabric.worker_count(), 1u);
    EXPECT_EQ(worker->worker_index(), 0u);

    ASSERT_TRUE(master->start(master_events).is_ok());
    ASSERT_TRUE(worker->start(worker_events).is_ok());

    const std::optional<PeerID> worker_as_seen = master_events.wait_for_peer();
    const std::optional<PeerID> master_as_seen = worker_events.wait_for_peer();
    ASSERT_TRUE(worker_as_seen.has_value());
    ASSERT_TRUE(master_as_seen.has_value());
    EXPECT_TRUE(worker_as_seen->valid());
    EXPECT_TRUE(master_as_seen->valid());

    const Bytes outbound = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
    const Bytes inbound = {std::byte{0x0A}, std::byte{0x0B}};
    ASSERT_EQ(master->send(*worker_as_seen, MessageType::TaskDispatch, view_of(outbound)),
              SendStatus::Queued);
    ASSERT_EQ(worker->send(*master_as_seen, MessageType::TaskResult, view_of(inbound)),
              SendStatus::Queued);

    const std::optional<ReceivedFrame> at_worker = worker_events.wait_for_frame();
    const std::optional<ReceivedFrame> at_master = master_events.wait_for_frame();
    ASSERT_TRUE(at_worker.has_value());
    ASSERT_TRUE(at_master.has_value());

    EXPECT_EQ(at_worker->peer, *master_as_seen);
    EXPECT_EQ(at_worker->type, MessageType::TaskDispatch);
    EXPECT_EQ(at_worker->flags, 0u);
    EXPECT_EQ(at_worker->payload, outbound);

    EXPECT_EQ(at_master->peer, *worker_as_seen);
    EXPECT_EQ(at_master->type, MessageType::TaskResult);
    EXPECT_EQ(at_master->flags, 0u);
    EXPECT_EQ(at_master->payload, inbound);

    worker->stop();
    master->stop();
}

/** @brief Tests that a payload below the policy's byte gate stays local. */
TEST(Unit_DistributedTest, PolicyKeepsTinyPayloadLocal)
{
    const std::vector<WorkerLoad> workers{live_worker(1, 0, K_AMPLE_MEMORY, 50)};

    EXPECT_FALSE(pick(workers, 1024).has_value());
}

/** @brief Tests that the worker with the shorter round trip wins when load is equal. */
TEST(Unit_DistributedTest, PolicyPicksLowerRttWorker)
{
    const std::vector<WorkerLoad> workers{
        live_worker(1, 0, K_AMPLE_MEMORY, 50000),
        live_worker(2, 0, K_AMPLE_MEMORY, 100),
    };

    const std::optional<int> picked = pick(workers, K_LARGE_PAYLOAD);
    ASSERT_TRUE(picked.has_value());
    EXPECT_EQ(*picked, 2);
}

/** @brief Tests that the worker with fewer tasks in flight wins when round trips are equal. */
TEST(Unit_DistributedTest, PolicyPrefersLessLoadedWhenEquallyNear)
{
    const std::vector<WorkerLoad> workers{
        live_worker(1, 5, K_AMPLE_MEMORY, 200),
        live_worker(2, 1, K_AMPLE_MEMORY, 200),
    };

    const std::optional<int> picked = pick(workers, K_LARGE_PAYLOAD);
    ASSERT_TRUE(picked.has_value());
    EXPECT_EQ(*picked, 2);
}

/** @brief Tests that a worker with less free memory than the payload is not picked. */
TEST(Unit_DistributedTest, PolicyRejectsInsufficientHeadroom)
{
    const std::vector<WorkerLoad> workers{live_worker(1, 0, K_LARGE_PAYLOAD / 2, 100)};

    EXPECT_FALSE(pick(workers, K_LARGE_PAYLOAD).has_value());
}

/** @brief Tests that the policy picks nobody when no worker is alive. */
TEST(Unit_DistributedTest, PolicyFallsBackLocalWhenNoLiveWorker)
{
    WorkerLoad dead = live_worker(1, 0, K_AMPLE_MEMORY, 100);
    dead.alive = false;
    const std::vector<WorkerLoad> workers{dead};

    EXPECT_FALSE(pick(workers, K_LARGE_PAYLOAD).has_value());
}

/** @brief Tests that a free-memory figure of zero counts as unknown and keeps the worker. */
TEST(Unit_DistributedTest, PolicyTreatsZeroFreeMemAsUnknown)
{
    const std::vector<WorkerLoad> workers{live_worker(1, 0, 0, 100)};

    const std::optional<int> picked = pick(workers, K_LARGE_PAYLOAD);
    ASSERT_TRUE(picked.has_value());
    EXPECT_EQ(*picked, 1);
}

/** @brief Tests that a result naming more outputs than its payload holds is refused. */
TEST(Unit_DistributedProtocolFuzz, TaskResultRefusesACountItCannotHold)
{
    Bytes payload;
    FieldWriter writer(payload);
    writer.put_u64(99);
    writer.put_enum(TaskResultStatus::Succeeded);
    writer.put_u32(1000);
    ASSERT_EQ(writer.status(), CodecStatus::Ok);

    TaskResultMessage message;
    const DecodeResult result = TaskResultMessage::decode(view_of(payload), message);
    EXPECT_EQ(result.status, CodecStatus::OutOfRange);
    EXPECT_EQ(result.offset, 9u);
    EXPECT_TRUE(message.outputs.empty());
}

/** @brief Tests that every decoder refuses or accepts a damaged payload without leaving it. */
TEST(Unit_DistributedProtocolFuzz, SeededMutationsDecodeOrFailInsideThePayload)
{
    std::mt19937_64 random(K_SEED);
    const std::vector<Sample> samples = every_sample();
    ASSERT_EQ(samples.size(), K_MESSAGE_TYPE_COUNT);

    for (const Sample& sample : samples)
    {
        SCOPED_TRACE(message_type_name(sample.type));
        ASSERT_TRUE(sample.decode(view_of(sample.payload)).ok());

        for (int round = 0; round < K_MUTATIONS_PER_SAMPLE; ++round)
        {
            const Bytes damaged = mutated(sample.payload, random);
            const DecodeResult result = sample.decode(view_of(damaged));
            EXPECT_LE(result.offset, damaged.size()) << "mutation " << round;
            if (result.ok())
                EXPECT_EQ(result.offset, 0u) << "mutation " << round;
        }
    }
}

/** @brief Tests that a random header passes only with every field inside its range. */
TEST(Unit_DistributedProtocolFuzz, SeededHeadersPassOnlyInsideTheirRanges)
{
    std::mt19937_64 random(K_SEED);
    int accepted = 0;
    for (int round = 0; round < K_RANDOM_HEADERS; ++round)
    {
        Bytes bytes(K_FRAME_HEADER_BYTES);
        for (std::byte& value : bytes)
            value = random_byte(random);

        // Three headers in four carry the magic and this build's version, so the later checks run.
        if (round % 4 != 0)
        {
            std::copy(K_FRAME_MAGIC.begin(), K_FRAME_MAGIC.end(), bytes.begin());
            bytes[4] = static_cast<std::byte>(K_PROTOCOL_VERSION & 0xFFu);
            bytes[5] = static_cast<std::byte>(K_PROTOCOL_VERSION >> 8);
            bytes[6] = static_cast<std::byte>(random() % (K_LAST_MESSAGE_TYPE + 2u));
            bytes[7] = static_cast<std::byte>(random() % 3);
            bytes[10] = static_cast<std::byte>(random() % 0x12);
            bytes[11] = std::byte{0x00};
        }

        FrameHeader header;
        if (decode_frame_header(view_of(bytes), header) != FrameStatus::Ok)
            continue;

        ++accepted;
        EXPECT_EQ(header.version, K_PROTOCOL_VERSION);
        EXPECT_TRUE(is_message_type(static_cast<std::uint8_t>(header.type)));
        EXPECT_LE(header.payload_length, K_MAX_FRAME_PAYLOAD);
        if (header.flags != 0)
        {
            EXPECT_EQ(header.flags, K_FRAME_FLAG_LAST_CHUNK);
            EXPECT_EQ(header.type, MessageType::BufferChunk);
        }
    }

    EXPECT_GT(accepted, 0) << "no random header passed, so the checks above never ran";
}
