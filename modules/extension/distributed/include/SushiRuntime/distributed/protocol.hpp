/**************************************************************************/
/* protocol.hpp                                                           */
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
 * @file protocol.hpp
 * @brief Declares the distributed wire protocol: identifiers, frames, serializers and messages.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <cstdint>
#include <vector>
#include <cstring>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/task/task_types.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Defines the wire protocol version to prevent incompatible nodes from communicating.
         *
         * This value is exchanged during the initial handshake and validated by the master to ensure struct layout matches.
         */
        constexpr uint32_t PROTOCOL_VERSION = 1;

        /**
         * @brief Establishes a maximum size for incoming network frames to prevent memory exhaustion.
         *
         * The transport layer validates the incoming frame length against this constant before allocating a reception buffer.
         */
        constexpr uint32_t MAX_FRAME_BYTES = 256u * 1024u * 1024u;

        /**
         * @brief Provides a network-safe identifier representing a distributed memory allocation.
         *
         * Workers use this globally unique ID to resolve local Unified Shared Memory (USM) pointers via their buffer cache.
         */
        struct GlobalBufferID
        {
            uint64_t value = 0;

            constexpr bool operator==(const GlobalBufferID& o) const { return value == o.value; }
            constexpr bool operator!=(const GlobalBufferID& o) const { return value != o.value; }
            constexpr bool valid() const { return value != 0; }
        };

        /**
         * @brief Tracks the identity, size, and coherence state of a distributed buffer.
         *
         * The master increments the version on write operations, forcing workers to fetch fresh data when their cached version becomes stale.
         */
        struct BufferHandle
        {
            GlobalBufferID id;
            uint32_t       version    = 0;
            uint64_t       size_bytes = 0;
        };

        /**
         * @brief Defines the available message types for the distributed wire protocol.
         *
         * Embedded as a single byte within the frame header to route payloads to appropriate handlers.
         */
        enum class MsgType : uint8_t
        {
            REGISTER_HELLO = 1, /**< worker -> master: I am alive; here is my OpID set. */
            REGISTER_ACK,       /**< master -> worker: agreed common kernel set. */
            TASK_DISPATCH,      /**< master -> worker: run this OpID with these handles. */
            BUFFER_REQUEST,     /**< worker -> master: I lack {id,version}; send bytes. */
            BUFFER_PUSH,        /**< master -> worker: raw bytes for a handle. */
            TASK_RESULT,        /**< worker -> master: task done; output handles ready. */
            HEARTBEAT,          /**< worker -> master: unsolicited liveness ping. */
            PING,               /**< master -> worker: RTT probe carrying a master token. */
            PONG                /**< worker -> master: echoes the token + reports free_mem. */
        };

        /**
         * @brief Defines the standard header structure for all network frames.
         *
         * Precedes the variable-length payload to provide the message type and exact byte count for allocation.
         */
        struct FrameHeader
        {
            uint32_t length;
            MsgType  type;
        };

        // Values are written raw and length-prefixed; both ends run the same binary.

        /**
         * @brief Serializes trivially-copyable values and binary blobs into a contiguous byte buffer.
         *
         * Appends raw memory representations sequentially to an internal vector for network transmission.
         */
        class ByteWriter
        {
            public:
                template<typename T>
                void put(const T& v)
                {
                    static_assert(std::is_trivially_copyable_v<T>, "ByteWriter::put needs trivially copyable T");
                    const auto* p = reinterpret_cast<const std::byte*>(&v);
                    buf_.insert(buf_.end(), p, p + sizeof(T));
                }

                /**
                 * @brief Appends a length-prefixed raw byte array to the buffer.
                 *
                 * Writes the size first as a 64-bit integer, followed by the actual payload bytes.
                 *
                 * @param data Pointer to the start of the binary data.
                 * @param bytes The number of bytes to read from the data pointer.
                 */
                void put_blob(const void* data, uint64_t bytes)
                {
                    put(bytes);
                    const auto* p = reinterpret_cast<const std::byte*>(data);
                    buf_.insert(buf_.end(), p, p + bytes);
                }

                SushiRuntime::span<const std::byte> view() const { return { buf_.data(), buf_.size() }; }
                std::vector<std::byte> take() { return std::move(buf_); }

            private:
                std::vector<std::byte> buf_;
        };

        /**
         * @brief Deserializes trivially-copyable values and binary blobs from a byte span.
         *
         * Tracks a read cursor and bounds-checks all extractions to safely decode network payloads.
         */
        class ByteReader
        {
            public:
                explicit ByteReader(SushiRuntime::span<const std::byte> data) : data_(data) {}

                template<typename T>
                T get()
                {
                    static_assert(std::is_trivially_copyable_v<T>, "ByteReader::get needs trivially copyable T");
                    T v{};
                    if (pos_ + sizeof(T) <= data_.size())
                    {
                        std::memcpy(&v, data_.data() + pos_, sizeof(T));
                        pos_ += sizeof(T);
                    }
                    else
                    {
                        ok_ = false;
                    }
                    return v;
                }

                /**
                 * @brief Extracts a length-prefixed raw byte array from the current position.
                 *
                 * Reads the 64-bit length prefix, then returns a subspan of the payload while advancing the cursor.
                 *
                 * @return A span viewing the extracted data block, or an empty span if a bounds check fails.
                 */
                SushiRuntime::span<const std::byte> get_blob()
                {
                    uint64_t bytes = get<uint64_t>();
                    // The check subtracts because pos_ + bytes can overflow on a wire value.
                    if (!ok_ || bytes > data_.size() - pos_)
                    {
                        ok_ = false;
                        return {};
                    }
                    auto s = data_.subspan(pos_, bytes);
                    pos_ += bytes;
                    return s;
                }

                bool ok() const { return ok_; }
                bool eof() const { return pos_ >= data_.size(); }

            private:
                SushiRuntime::span<const std::byte> data_;
                size_t pos_ = 0;
                bool   ok_  = true;
        };

        // Message bodies are serialized by hand because they carry variable-length lists and blobs.

        /**
         * @brief Encodes a master-to-worker command to execute an offloaded task.
         *
         * Serializes the operation ID, metadata, and buffer dependencies into a network message.
         */
        struct TaskDispatchMsg
        {
            uint64_t                  task_id = 0;
            Graph::OpID               op_id;
            Graph::TaskMetadata       meta;
            std::vector<BufferHandle> inputs;
            std::vector<BufferHandle> outputs;

            std::vector<std::byte> serialize() const
            {
                ByteWriter w;
                w.put(task_id);
                w.put(op_id.value);
                // meta.name is not serialized; a master pointer means nothing on a worker.
                w.put(meta.task_type);
                w.put(meta.op_id);
                w.put(meta.priority);
                w.put(meta.numa_affinity);
                for (uint64_t p : meta.params) w.put(p);
                w.put<uint32_t>(static_cast<uint32_t>(inputs.size()));
                for (const auto& h : inputs)  w.put(h);
                w.put<uint32_t>(static_cast<uint32_t>(outputs.size()));
                for (const auto& h : outputs) w.put(h);
                return w.take();
            }

            static TaskDispatchMsg deserialize(SushiRuntime::span<const std::byte> bytes)
            {
                ByteReader r(bytes);
                TaskDispatchMsg m;
                m.task_id         = r.get<uint64_t>();
                m.op_id.value     = r.get<uint32_t>();
                // name keeps its default literal; see serialize().
                m.meta.task_type     = r.get<Graph::TaskType>();
                m.meta.op_id         = r.get<Graph::OpID>();
                m.meta.priority      = r.get<int>();
                m.meta.numa_affinity = r.get<int>();
                for (uint64_t& p : m.meta.params) p = r.get<uint64_t>();
                uint32_t ni   = r.get<uint32_t>();
                for (uint32_t i = 0; i < ni && r.ok(); ++i) m.inputs.push_back(r.get<BufferHandle>());
                uint32_t no   = r.get<uint32_t>();
                for (uint32_t i = 0; i < no && r.ok(); ++i) m.outputs.push_back(r.get<BufferHandle>());
                return m;
            }
        };

        /**
         * @brief Encodes a worker-to-master notification containing task completion status and output data.
         *
         * Serializes the execution outcome alongside the mutated buffer versions and their raw byte payloads inline.
         */
        struct TaskResultMsg
        {
            uint64_t                  task_id = 0;
            bool                      success = true;
            std::vector<BufferHandle> outputs;     /**< new versions produced on the worker. */
            std::vector<std::vector<std::byte>> output_data; /**< bytes for each output, parallel to outputs. */

            std::vector<std::byte> serialize() const
            {
                ByteWriter w;
                w.put(task_id);
                w.put<uint8_t>(success ? 1 : 0);
                w.put<uint32_t>(static_cast<uint32_t>(outputs.size()));
                for (size_t i = 0; i < outputs.size(); ++i)
                {
                    w.put(outputs[i]);
                    const auto& blob = i < output_data.size() ? output_data[i]
                                                              : std::vector<std::byte>{};
                    w.put_blob(blob.data(), blob.size());
                }
                return w.take();
            }

            static TaskResultMsg deserialize(SushiRuntime::span<const std::byte> bytes)
            {
                ByteReader r(bytes);
                TaskResultMsg m;
                m.task_id   = r.get<uint64_t>();
                m.success   = r.get<uint8_t>() != 0;
                uint32_t n  = r.get<uint32_t>();
                for (uint32_t i = 0; i < n && r.ok(); ++i)
                {
                    BufferHandle h = r.get<BufferHandle>();
                    auto blob = r.get_blob();
                    if (!r.ok()) break;
                    m.outputs.push_back(h);
                    m.output_data.emplace_back(blob.begin(), blob.end());
                }
                return m;
            }
        };

        /**
         * @brief Encodes a unidirectional transfer of raw buffer bytes.
         *
         * Serializes a buffer handle alongside its raw byte contents to satisfy remote data requests or cache pushes.
         */
        struct BufferPushMsg
        {
            BufferHandle           handle;
            std::vector<std::byte> data;

            std::vector<std::byte> serialize() const
            {
                ByteWriter w;
                w.put(handle);
                w.put_blob(data.data(), data.size());
                return w.take();
            }

            static BufferPushMsg deserialize(SushiRuntime::span<const std::byte> bytes)
            {
                ByteReader r(bytes);
                BufferPushMsg m;
                m.handle = r.get<BufferHandle>();
                auto blob = r.get_blob();
                m.data.assign(blob.begin(), blob.end());
                return m;
            }
        };

        /**
         * @brief Encodes a worker-to-master request for a specific buffer version.
         *
         * Serializes the desired global identifier and version number to trigger a master push.
         */
        struct BufferRequestMsg
        {
            GlobalBufferID id;
            uint32_t       version = 0;

            std::vector<std::byte> serialize() const
            {
                ByteWriter w;
                w.put(id.value);
                w.put(version);
                return w.take();
            }

            static BufferRequestMsg deserialize(SushiRuntime::span<const std::byte> bytes)
            {
                ByteReader r(bytes);
                BufferRequestMsg m;
                m.id.value = r.get<uint64_t>();
                m.version  = r.get<uint32_t>();
                return m;
            }
        };

        /**
         * @brief Encodes a master-to-worker timing probe for measuring network latency.
         *
         * Carries a master-generated clock token which the worker must echo back verbatim.
         */
        struct PingMsg
        {
            uint64_t token = 0; /**< opaque to the worker; master steady-clock ticks. */

            std::vector<std::byte> serialize() const
            {
                ByteWriter w;
                w.put(token);
                return w.take();
            }

            static PingMsg deserialize(SushiRuntime::span<const std::byte> bytes)
            {
                ByteReader r(bytes);
                PingMsg m;
                m.token = r.get<uint64_t>();
                return m;
            }
        };

        /**
         * @brief Encodes a worker-to-master reply to a timing probe, including current resource state.
         *
         * Echoes the master's clock token and provides the worker's available device memory headroom.
         */
        struct PongMsg
        {
            uint64_t token    = 0; /**< echoed from the PingMsg. */
            uint64_t free_mem = 0; /**< worker device memory headroom (bytes), 0 = unknown. */

            std::vector<std::byte> serialize() const
            {
                ByteWriter w;
                w.put(token);
                w.put(free_mem);
                return w.take();
            }

            static PongMsg deserialize(SushiRuntime::span<const std::byte> bytes)
            {
                ByteReader r(bytes);
                PongMsg m;
                m.token    = r.get<uint64_t>();
                m.free_mem = r.get<uint64_t>();
                return m;
            }
        };

    } // namespace Distributed
} // namespace SushiRuntime
