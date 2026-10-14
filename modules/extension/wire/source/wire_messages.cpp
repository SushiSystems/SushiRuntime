/**************************************************************************/
/* wire_messages.cpp                                                      */
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
 * @file wire_messages.cpp
 * @brief Encodes and decodes the sixteen messages of protocol version 3.
 * @author Mustafa Garip
 * @see modules/extension/wire/README.md
 */

#include <SushiRuntime/wire/wire_messages.hpp>

namespace SushiRuntime::Distributed::Wire
{
    namespace
    {
        /** @brief Holds the encoded size of one operation identifier. */
        constexpr std::size_t K_OP_ID_BYTES = 4;

        /** @brief Appends a handle as id, version, size: 20 bytes. */
        void put_handle(FieldWriter& writer, const BufferHandle& handle)
        {
            writer.put_u64(handle.id.value);
            writer.put_u32(handle.version);
            writer.put_u64(handle.size_bytes);
        }

        /** @brief Reads a handle. */
        BufferHandle read_handle(FieldReader& reader) noexcept
        {
            BufferHandle handle;
            handle.id.value = reader.read_u64();
            handle.version = reader.read_u32();
            handle.size_bytes = reader.read_u64();
            return handle;
        }

        /** @brief Appends a list of handles behind its count. */
        void put_handles(FieldWriter& writer, const std::vector<BufferHandle>& handles)
        {
            writer.put_count(handles.size());
            for (const BufferHandle& handle : handles)
                put_handle(writer, handle);
        }

        /** @brief Reads a list of handles; reserves once, after the count passed its check. */
        void read_handles(FieldReader& reader, std::vector<BufferHandle>& out)
        {
            const std::uint32_t count = reader.read_count(K_BUFFER_HANDLE_BYTES);
            out.clear();
            out.reserve(count);
            for (std::uint32_t index = 0; index < count; ++index)
                out.push_back(read_handle(reader));
        }

    } // namespace

    CodecStatus HelloMessage::encode(std::vector<std::byte>& out) const
    {
        FieldWriter writer(out);
        writer.put_bytes({worker_nonce.data(), worker_nonce.size()});
        writer.put_string(build_id);
        writer.put_string(node_name);
        return writer.status();
    }

    DecodeResult HelloMessage::decode(SushiRuntime::span<const std::byte> payload,
                                      HelloMessage& out)
    {
        FieldReader reader(payload);
        reader.read_bytes({out.worker_nonce.data(), out.worker_nonce.size()});
        out.build_id = reader.read_string();
        out.node_name = reader.read_string();
        return reader.finish();
    }

    CodecStatus ChallengeMessage::encode(std::vector<std::byte>& out) const
    {
        FieldWriter writer(out);
        writer.put_bytes({master_nonce.data(), master_nonce.size()});
        writer.put_bytes({master_proof.data(), master_proof.size()});
        writer.put_u64(session_id);
        return writer.status();
    }

    DecodeResult ChallengeMessage::decode(SushiRuntime::span<const std::byte> payload,
                                          ChallengeMessage& out)
    {
        FieldReader reader(payload);
        reader.read_bytes({out.master_nonce.data(), out.master_nonce.size()});
        reader.read_bytes({out.master_proof.data(), out.master_proof.size()});
        out.session_id = reader.read_u64();
        return reader.finish();
    }

    CodecStatus ProofMessage::encode(std::vector<std::byte>& out) const
    {
        FieldWriter writer(out);
        writer.put_bytes({worker_proof.data(), worker_proof.size()});
        writer.put_count(ops.size());
        for (const OperationID& op : ops)
            writer.put_u32(op.value);
        writer.put_u32(max_jobs);
        writer.put_u64(cache_budget);
        return writer.status();
    }

    DecodeResult ProofMessage::decode(SushiRuntime::span<const std::byte> payload,
                                      ProofMessage& out)
    {
        FieldReader reader(payload);
        reader.read_bytes({out.worker_proof.data(), out.worker_proof.size()});

        const std::uint32_t count = reader.read_count(K_OP_ID_BYTES);
        out.ops.clear();
        out.ops.reserve(count);
        for (std::uint32_t index = 0; index < count; ++index)
            out.ops.push_back(OperationID(reader.read_u32()));

        out.max_jobs = reader.read_u32();
        out.cache_budget = reader.read_u64();
        return reader.finish();
    }

    CodecStatus WelcomeMessage::encode(std::vector<std::byte>& out) const
    {
        FieldWriter writer(out);
        writer.put_u32(worker_id);
        writer.put_u32(liveness_timeout_ms);
        writer.put_u32(ping_interval_ms);
        writer.put_u32(chunk_bytes);
        return writer.status();
    }

    DecodeResult WelcomeMessage::decode(SushiRuntime::span<const std::byte> payload,
                                        WelcomeMessage& out)
    {
        FieldReader reader(payload);
        out.worker_id = reader.read_u32();
        out.liveness_timeout_ms = reader.read_u32();
        out.ping_interval_ms = reader.read_u32();
        out.chunk_bytes = reader.read_u32();
        return reader.finish();
    }

    CodecStatus RejectMessage::encode(std::vector<std::byte>& out) const
    {
        FieldWriter writer(out);
        writer.put_enum(reason);
        writer.put_u16(sender_version);
        writer.put_string(text);
        return writer.status();
    }

    DecodeResult RejectMessage::decode(SushiRuntime::span<const std::byte> payload,
                                       RejectMessage& out)
    {
        FieldReader reader(payload);
        out.reason = reader.read_enum(RejectReason::Version, RejectReason::Busy);
        out.sender_version = reader.read_u16();
        out.text = reader.read_string();
        return reader.finish();
    }

    CodecStatus GoodbyeMessage::encode(std::vector<std::byte>& out) const
    {
        FieldWriter writer(out);
        writer.put_enum(reason);
        writer.put_string(text);
        return writer.status();
    }

    DecodeResult GoodbyeMessage::decode(SushiRuntime::span<const std::byte> payload,
                                        GoodbyeMessage& out)
    {
        FieldReader reader(payload);
        out.reason = reader.read_enum(GoodbyeReason::Shutdown, GoodbyeReason::Detach);
        out.text = reader.read_string();
        return reader.finish();
    }

    CodecStatus TaskDispatchMessage::encode(std::vector<std::byte>& out) const
    {
        if (name.size() > K_MAX_DISPATCH_NAME_BYTES || parameter_count > parameters.size())
            return CodecStatus::TooLong;

        FieldWriter writer(out);
        writer.put_u64(task_id);
        writer.put_u32(operation.value);
        writer.put_string(name);
        writer.put_u8(parameter_count);
        for (std::size_t index = 0; index < parameter_count; ++index)
            writer.put_u64(parameters[index]);
        put_handles(writer, inputs);
        put_handles(writer, outputs);
        return writer.status();
    }

    DecodeResult TaskDispatchMessage::decode(SushiRuntime::span<const std::byte> payload,
                                             TaskDispatchMessage& out)
    {
        FieldReader reader(payload);
        out.task_id = reader.read_u64();
        out.operation = OperationID(reader.read_u32());
        out.name.assign(reader.read_string());
        out.parameters.fill(0);
        out.parameter_count =
            reader.read_u8_at_most(static_cast<std::uint8_t>(K_MAX_DISPATCH_PARAMETERS));
        for (std::size_t index = 0; index < out.parameter_count; ++index)
            out.parameters[index] = reader.read_u64();
        read_handles(reader, out.inputs);
        read_handles(reader, out.outputs);
        return reader.finish();
    }

    CodecStatus TaskAcceptedMessage::encode(std::vector<std::byte>& out) const
    {
        FieldWriter writer(out);
        writer.put_u64(task_id);
        return writer.status();
    }

    DecodeResult TaskAcceptedMessage::decode(SushiRuntime::span<const std::byte> payload,
                                             TaskAcceptedMessage& out)
    {
        FieldReader reader(payload);
        out.task_id = reader.read_u64();
        return reader.finish();
    }

    CodecStatus TaskResultMessage::encode(std::vector<std::byte>& out) const
    {
        FieldWriter writer(out);
        writer.put_u64(task_id);
        writer.put_enum(status);
        put_handles(writer, outputs);
        return writer.status();
    }

    DecodeResult TaskResultMessage::decode(SushiRuntime::span<const std::byte> payload,
                                           TaskResultMessage& out)
    {
        FieldReader reader(payload);
        out.task_id = reader.read_u64();
        out.status =
            reader.read_enum(TaskResultStatus::Succeeded, TaskResultStatus::ShuttingDown);
        read_handles(reader, out.outputs);
        return reader.finish();
    }

    CodecStatus BufferRequestMessage::encode(std::vector<std::byte>& out) const
    {
        FieldWriter writer(out);
        writer.put_u64(id.value);
        writer.put_u32(version);
        return writer.status();
    }

    DecodeResult BufferRequestMessage::decode(SushiRuntime::span<const std::byte> payload,
                                              BufferRequestMessage& out)
    {
        FieldReader reader(payload);
        out.id.value = reader.read_u64();
        out.version = reader.read_u32();
        return reader.finish();
    }

    CodecStatus BufferChunkMessage::encode(std::vector<std::byte>& out) const
    {
        FieldWriter writer(out);
        writer.put_enum(purpose);
        writer.put_u64(task_id);
        put_handle(writer, handle);
        writer.put_u64(offset);
        writer.put_bytes(data);
        return writer.status();
    }

    DecodeResult BufferChunkMessage::decode(SushiRuntime::span<const std::byte> payload,
                                            BufferChunkMessage& out)
    {
        FieldReader reader(payload);
        out.purpose = reader.read_enum(ChunkPurpose::PullReply, ChunkPurpose::TaskOutput);
        out.task_id = reader.read_u64();
        out.handle = read_handle(reader);
        out.offset = reader.read_u64();
        out.data = reader.read_rest();
        return reader.finish();
    }

    CodecStatus BufferAbortMessage::encode(std::vector<std::byte>& out) const
    {
        FieldWriter writer(out);
        writer.put_u64(id.value);
        writer.put_u32(version);
        writer.put_u64(task_id);
        writer.put_enum(reason);
        return writer.status();
    }

    DecodeResult BufferAbortMessage::decode(SushiRuntime::span<const std::byte> payload,
                                            BufferAbortMessage& out)
    {
        FieldReader reader(payload);
        out.id.value = reader.read_u64();
        out.version = reader.read_u32();
        out.task_id = reader.read_u64();
        out.reason = reader.read_enum(AbortReason::Stale, AbortReason::Gone);
        return reader.finish();
    }

    CodecStatus BufferReleaseMessage::encode(std::vector<std::byte>& out) const
    {
        FieldWriter writer(out);
        writer.put_u64(id.value);
        return writer.status();
    }

    DecodeResult BufferReleaseMessage::decode(SushiRuntime::span<const std::byte> payload,
                                              BufferReleaseMessage& out)
    {
        FieldReader reader(payload);
        out.id.value = reader.read_u64();
        return reader.finish();
    }

    CodecStatus PingMessage::encode(std::vector<std::byte>& out) const
    {
        FieldWriter writer(out);
        writer.put_u64(token);
        return writer.status();
    }

    DecodeResult PingMessage::decode(SushiRuntime::span<const std::byte> payload,
                                     PingMessage& out)
    {
        FieldReader reader(payload);
        out.token = reader.read_u64();
        return reader.finish();
    }

    CodecStatus PongMessage::encode(std::vector<std::byte>& out) const
    {
        FieldWriter writer(out);
        writer.put_u64(token);
        writer.put_u64(free_mem);
        return writer.status();
    }

    DecodeResult PongMessage::decode(SushiRuntime::span<const std::byte> payload,
                                     PongMessage& out)
    {
        FieldReader reader(payload);
        out.token = reader.read_u64();
        out.free_mem = reader.read_u64();
        return reader.finish();
    }

    CodecStatus HeartbeatMessage::encode(std::vector<std::byte>& out) const
    {
        FieldWriter writer(out);
        writer.put_u32(queue_depth);
        writer.put_u64(oldest_held_task_id);
        writer.put_u64(cache_bytes);
        return writer.status();
    }

    DecodeResult HeartbeatMessage::decode(SushiRuntime::span<const std::byte> payload,
                                          HeartbeatMessage& out)
    {
        FieldReader reader(payload);
        out.queue_depth = reader.read_u32();
        out.oldest_held_task_id = reader.read_u64();
        out.cache_bytes = reader.read_u64();
        return reader.finish();
    }
} // namespace SushiRuntime::Distributed::Wire
