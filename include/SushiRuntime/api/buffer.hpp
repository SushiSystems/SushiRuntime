/**************************************************************************/
/* buffer.hpp                                                             */
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

#include <cstddef>
#include <memory>
#include <utility>

#include <SushiRuntime/execution/runtime_context.hpp>

namespace SushiRuntime
{
    namespace API
    {
        /**
         * @brief Owning handle to a typed block of unified shared memory.
         *
         * A Buffer is the fluent API's unit of data: a typed USM allocation that
         * the runtime tracks by owning queue, so a task touching it is dispatched
         * to the device that holds it. Passing a Buffer to Graph::add() is what
         * lets the runtime register the task's reads and writes automatically — the
         * handle's address is the identity the dependency tracker keys on.
         *
         * The memory is shared USM, so the host may index it directly (to seed or
         * read back data) and a device kernel may capture data() and use it on the
         * device. The handle owns the allocation and frees it on destruction, so it
         * must outlive every graph run that touches it. Move-only.
         *
         * @tparam T Element type. Must be trivially copyable for device use.
         */
        template <typename T>
        class Buffer
        {
            public:
                /** @brief Constructs an empty handle that owns no memory. */
                Buffer() = default;

                /**
                 * @brief Allocates @p count elements of shared USM on a NUMA node.
                 *
                 * Allocation goes through the context so the owning queue is
                 * recorded; do not construct a Buffer over foreign USM.
                 *
                 * @param ctx     The runtime context that owns the allocation.
                 * @param count   Number of elements to allocate.
                 * @param numa_id Target NUMA node; selects the owning queue.
                 */
                Buffer(std::shared_ptr<Execution::RuntimeContext> ctx,
                       std::size_t count, int numa_id = 0)
                    : ctx_(std::move(ctx)),
                      ptr_(ctx_->template malloc_shared<T>(count, numa_id)),
                      size_(count),
                      numa_id_(numa_id)
                {
                }

                /** @brief Frees the owned allocation, if any. */
                ~Buffer()
                {
                    reset();
                }

                Buffer(const Buffer&) = delete;
                Buffer& operator=(const Buffer&) = delete;

                /** @brief Takes ownership of @p other's allocation. */
                Buffer(Buffer&& other) noexcept
                    : ctx_(std::move(other.ctx_)),
                      ptr_(other.ptr_),
                      size_(other.size_),
                      numa_id_(other.numa_id_)
                {
                    other.ptr_ = nullptr;
                    other.size_ = 0;
                }

                /** @brief Frees this handle's memory, then takes @p other's. */
                Buffer& operator=(Buffer&& other) noexcept
                {
                    if (this != &other)
                    {
                        reset();
                        ctx_ = std::move(other.ctx_);
                        ptr_ = other.ptr_;
                        size_ = other.size_;
                        numa_id_ = other.numa_id_;
                        other.ptr_ = nullptr;
                        other.size_ = 0;
                    }
                    return *this;
                }

                /** @brief Pointer to the first element; capture this in a kernel. */
                T* data() const noexcept { return ptr_; }

                /** @brief Number of elements. */
                std::size_t size() const noexcept { return size_; }

                /** @brief True if the handle owns no allocation. */
                bool empty() const noexcept { return ptr_ == nullptr; }

                /** @brief NUMA node the allocation is pinned to. */
                int numa_id() const noexcept { return numa_id_; }

                /** @brief Host-side element access (shared USM is host-addressable). */
                T& operator[](std::size_t i) const noexcept { return ptr_[i]; }

                /**
                 * @brief The dependency-tracking identity of this buffer.
                 *
                 * Graph::add() passes this to the dependency tracker as the
                 * read/write key. It is the allocation's base address.
                 *
                 * @return The base pointer as a void*, or nullptr when empty.
                 */
                void* dependency_key() const noexcept { return static_cast<void*>(ptr_); }

            private:
                /** @brief Frees the owned allocation and clears the handle. */
                void reset() noexcept
                {
                    if (ptr_ != nullptr && ctx_)
                        ctx_->free_usm(ptr_);
                    ptr_ = nullptr;
                    size_ = 0;
                }

                std::shared_ptr<Execution::RuntimeContext> ctx_;
                T* ptr_ = nullptr;
                std::size_t size_ = 0;
                int numa_id_ = 0;
        };

        /**
         * @brief A time-evolving field the runtime double-buffers across steps.
         *
         * Many stepped workloads (stencils, PDE solvers, the digital-twin driver)
         * must read the current state and write the next one — they cannot update
         * in place. A State owns two equally sized buffers, current() and next(),
         * and the runtime swaps their roles after each step (advance()), an O(1)
         * pointer exchange. The user writes one step as "read current, write next"
         * and never manages the ping-pong.
         *
         * @tparam T Element type. Must be trivially copyable for device use.
         */
        template <typename T>
        class State
        {
            public:
                /** @brief Constructs an empty state that owns no memory. */
                State() = default;

                /**
                 * @brief Allocates both buffers of @p count elements on a NUMA node.
                 * @param ctx     The runtime context that owns the allocations.
                 * @param count   Number of elements in each buffer.
                 * @param numa_id Target NUMA node for both buffers.
                 */
                State(std::shared_ptr<Execution::RuntimeContext> ctx,
                      std::size_t count, int numa_id = 0)
                    : cur_(ctx, count, numa_id),
                      next_(std::move(ctx), count, numa_id)
                {
                }

                /** @brief Pointer to the current state; the step reads from here. */
                T* current() const noexcept { return cur_.data(); }

                /** @brief Pointer to the next state; the step writes here. */
                T* next() const noexcept { return next_.data(); }

                /** @brief Number of elements in each buffer. */
                std::size_t size() const noexcept { return cur_.size(); }

                /** @brief True if the state owns no allocation. */
                bool empty() const noexcept { return cur_.empty(); }

                /** @brief Read dependency key (the current buffer's base address). */
                void* read_key() const noexcept { return cur_.dependency_key(); }

                /** @brief Write dependency key (the next buffer's base address). */
                void* write_key() const noexcept { return next_.dependency_key(); }

                /** @brief Host-side access to the current state's elements. */
                T& operator[](std::size_t i) const noexcept { return cur_[i]; }

                /**
                 * @brief Swaps current and next so the just-written state becomes
                 *        current. The runtime calls this once per step.
                 */
                void advance() noexcept
                {
                    using std::swap;
                    swap(cur_, next_);
                }

            private:
                Buffer<T> cur_;
                Buffer<T> next_;
        };

    } // namespace API

    using API::Buffer;
    using API::State;

} // namespace SushiRuntime
