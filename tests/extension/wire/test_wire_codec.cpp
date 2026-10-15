/**************************************************************************/
/* test_wire_codec.cpp                                                    */
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
 * @file test_wire_codec.cpp
 * @brief Tests the wire codec: seeded round trips and seeded fuzzing of payloads and headers.
 * @see tests/extension/wire/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/transport/buffer_handle.hpp>
#include <SushiRuntime/transport/transport.hpp>
#include <SushiRuntime/transport/message_type.hpp>
#include <SushiRuntime/wire/wire_codec.hpp>
#include <SushiRuntime/wire/wire_frame.hpp>
#include <SushiRuntime/wire/wire_messages.hpp>


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
        TaskDispatchMessage message;
        message.task_id = random();
        message.operation = OperationID(static_cast<std::uint32_t>(random()));

        // The name has every length a dispatch carries, the empty one among them.
        message.name.assign(random() % (K_MAX_DISPATCH_NAME_BYTES + 1), 'n');

        // The count runs from none to all, and the entries past it stay zero.
        message.parameter_count =
            static_cast<std::uint8_t>(random() % (K_MAX_DISPATCH_PARAMETERS + 1));
        for (std::size_t index = 0; index < message.parameter_count; ++index)
            message.parameters[index] = random();

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
} // namespace

/** @brief Tests that random dispatches cross a whole frame with every wire field intact. */
TEST(Medium_WireCodec, SeededDispatchesSurviveAWholeFrame)
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
        EXPECT_EQ(received.operation.value, sent.operation.value);
        EXPECT_EQ(received.name, sent.name);
        EXPECT_EQ(received.parameter_count, sent.parameter_count);
        for (std::size_t index = 0; index < K_MAX_DISPATCH_PARAMETERS; ++index)
            EXPECT_EQ(received.parameters[index], sent.parameters[index])
                << "parameter " << index;
        expect_same_handles(received.inputs, sent.inputs);
        expect_same_handles(received.outputs, sent.outputs);
    }
}

TEST(Medium_WireCodecFuzz, TaskResultRefusesACountItCannotHold)
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
TEST(Medium_WireCodecFuzz, SeededMutationsDecodeOrFailInsideThePayload)
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
TEST(Medium_WireCodecFuzz, SeededHeadersPassOnlyInsideTheirRanges)
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
