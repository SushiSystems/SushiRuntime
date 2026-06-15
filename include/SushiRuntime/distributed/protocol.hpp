/**************************************************************************/
/* protocol.hpp                                                           */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

#pragma once

//
// Distributed wire protocol.
//
// The whole distributed layer exists behind SUSHI_DISTRIBUTED (CMake option
// SUSHI_ENABLE_DISTRIBUTED, default OFF). When the option is off this header is
// never compiled into the library, so the offline single-machine build carries
// zero distributed code, zero extra dependencies, and zero ABI surface.
//
// The core idea: compiled code cannot travel the wire. A task is a C++ closure
// over local pointers, neither of which is meaningful on another machine. So we
// never serialize code or pointers — we serialize *references* to code that is
// already compiled into every node (OpID) plus *references* to data that each
// worker can resolve to its own local USM (GlobalBufferID + version).
//

#include <cstdint>
#include <vector>
#include <cstring>
#include <span>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/graph/task_types.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Network-safe stand-in for a void* USM pointer.
         *
         * The master assigns these; they are globally unique within a run. A
         * worker never sees a master pointer, only this id, which it resolves
         * against its own BufferCache to a local USM allocation.
         */
        struct GlobalBufferID
        {
            uint64_t value = 0;

            constexpr bool operator==(const GlobalBufferID& o) const { return value == o.value; }
            constexpr bool operator!=(const GlobalBufferID& o) const { return value != o.value; }
            constexpr bool valid() const { return value != 0; }
        };

        /**
         * @brief Coherence token: identity + version + size.
         *
         * Version is the heart of the stateful buffer cache. The master bumps it
         * whenever a task writes the buffer (WAW/WAR in the DependencyTracker).
         * A worker holding an older version treats its cache entry as stale and
         * pulls fresh bytes (pull-on-miss, master-authoritative).
         */
        struct BufferHandle
        {
            GlobalBufferID id;
            uint32_t       version    = 0;
            uint64_t       size_bytes = 0;
        };

        /**
         * @brief Message kinds on the wire. One byte in the frame header.
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
         * @brief On-wire frame: [4B length][1B type][payload...].
         *
         * length counts the payload bytes only (not the header). Kept tiny so
         * any ITransport (TCP, in-process) can frame messages identically.
         */
        struct FrameHeader
        {
            uint32_t length;
            MsgType  type;
        };

        // ---------------------------------------------------------------------
        // Tiny length-prefixed serializer. Both ends share the same binary and,
        // in the target lab, the same architecture/endianness, so we keep it raw
        // and trivially-copyable rather than pulling in a schema library.
        // ---------------------------------------------------------------------

        /** @brief Appends trivially-copyable values and sized blobs to a byte buffer. */
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

                /** @brief Writes a length-prefixed (uint64) raw blob. */
                void put_blob(const void* data, uint64_t bytes)
                {
                    put(bytes);
                    const auto* p = reinterpret_cast<const std::byte*>(data);
                    buf_.insert(buf_.end(), p, p + bytes);
                }

                std::span<const std::byte> view() const { return { buf_.data(), buf_.size() }; }
                std::vector<std::byte> take() { return std::move(buf_); }

            private:
                std::vector<std::byte> buf_;
        };

        /** @brief Reads back what ByteWriter produced. Bounds-checked via ok(). */
        class ByteReader
        {
            public:
                explicit ByteReader(std::span<const std::byte> data) : data_(data) {}

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

                /** @brief Reads a length-prefixed blob; returned span aliases the input buffer. */
                std::span<const std::byte> get_blob()
                {
                    uint64_t bytes = get<uint64_t>();
                    if (!ok_ || pos_ + bytes > data_.size())
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
                std::span<const std::byte> data_;
                size_t pos_ = 0;
                bool   ok_  = true;
        };

        // ---------------------------------------------------------------------
        // Message bodies. These are *logical* shapes; (de)serialization is done
        // explicitly with ByteWriter/ByteReader because they carry variable
        // length handle lists and blobs.
        // ---------------------------------------------------------------------

        /**
         * @brief master -> worker: execute one offloaded task.
         *
         * Carries no code and no pointers — only the OpID of a kernel already
         * compiled into the worker, the serializable TaskMetadata (params[8]),
         * and the input/output buffer handles to resolve against the cache.
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
                w.put(meta);
                w.put<uint32_t>(static_cast<uint32_t>(inputs.size()));
                for (const auto& h : inputs)  w.put(h);
                w.put<uint32_t>(static_cast<uint32_t>(outputs.size()));
                for (const auto& h : outputs) w.put(h);
                return w.take();
            }

            static TaskDispatchMsg deserialize(std::span<const std::byte> bytes)
            {
                ByteReader r(bytes);
                TaskDispatchMsg m;
                m.task_id     = r.get<uint64_t>();
                m.op_id.value = r.get<uint32_t>();
                m.meta        = r.get<Graph::TaskMetadata>();
                uint32_t ni   = r.get<uint32_t>();
                for (uint32_t i = 0; i < ni && r.ok(); ++i) m.inputs.push_back(r.get<BufferHandle>());
                uint32_t no   = r.get<uint32_t>();
                for (uint32_t i = 0; i < no && r.ok(); ++i) m.outputs.push_back(r.get<BufferHandle>());
                return m;
            }
        };

        /** @brief worker -> master: result is ready (lazy: bytes pulled only when needed). */
        struct TaskResultMsg
        {
            uint64_t                  task_id = 0;
            bool                      success = true;
            std::vector<BufferHandle> outputs; /**< new versions produced on the worker. */

            std::vector<std::byte> serialize() const
            {
                ByteWriter w;
                w.put(task_id);
                w.put<uint8_t>(success ? 1 : 0);
                w.put<uint32_t>(static_cast<uint32_t>(outputs.size()));
                for (const auto& h : outputs) w.put(h);
                return w.take();
            }

            static TaskResultMsg deserialize(std::span<const std::byte> bytes)
            {
                ByteReader r(bytes);
                TaskResultMsg m;
                m.task_id   = r.get<uint64_t>();
                m.success   = r.get<uint8_t>() != 0;
                uint32_t n  = r.get<uint32_t>();
                for (uint32_t i = 0; i < n && r.ok(); ++i) m.outputs.push_back(r.get<BufferHandle>());
                return m;
            }
        };

        /** @brief either direction: ship the raw bytes for one buffer version. */
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

            static BufferPushMsg deserialize(std::span<const std::byte> bytes)
            {
                ByteReader r(bytes);
                BufferPushMsg m;
                m.handle = r.get<BufferHandle>();
                auto blob = r.get_blob();
                m.data.assign(blob.begin(), blob.end());
                return m;
            }
        };

        /** @brief worker -> master: I need this {id,version}; please push it. */
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

            static BufferRequestMsg deserialize(std::span<const std::byte> bytes)
            {
                ByteReader r(bytes);
                BufferRequestMsg m;
                m.id.value = r.get<uint64_t>();
                m.version  = r.get<uint32_t>();
                return m;
            }
        };

        /**
         * @brief master -> worker RTT probe.
         *
         * The token is the master's own steady-clock value at send time. The
         * worker echoes it verbatim in a PongMsg; the master computes the round
         * trip purely from its own clock, so worker/master clock skew never enters
         * the measurement.
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

            static PingMsg deserialize(std::span<const std::byte> bytes)
            {
                ByteReader r(bytes);
                PingMsg m;
                m.token = r.get<uint64_t>();
                return m;
            }
        };

        /**
         * @brief worker -> master RTT reply + live device-memory headroom.
         *
         * Carries the PingMsg token unchanged (so the master can derive RTT) plus
         * the worker's current free device memory, which the offload policy uses as
         * a headroom gate before dispatching a payload to that worker.
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

            static PongMsg deserialize(std::span<const std::byte> bytes)
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
