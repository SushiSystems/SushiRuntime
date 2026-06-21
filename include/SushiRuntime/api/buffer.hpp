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
        namespace detail
        {
            /**
             * @brief Indirection a stepped kernel reads to find its data each step.
             *
             * A State kernel captures a shared_ptr to one of these instead of raw
             * pointers. Because the kernel's host-side body runs once per step (at
             * submit time), it reads cur/next fresh every step. The runtime updates
             * these two fields when it swaps the State between steps, so one
             * compiled node follows the double-buffer without being recompiled.
             *
             * @tparam T Element type of the buffers this view points into.
             */
            template <typename T>
            struct StateView
            {
                T* cur = nullptr;   /**< Current-state base pointer for this step. */
                T* next = nullptr;  /**< Next-state base pointer for this step. */
            };
        } // namespace detail

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
                 * @brief Allocates @p count elements of shared USM on one device.
                 *
                 * Allocation is pinned to @p device_index and its owner recorded, so
                 * buffers used together (e.g. a State's two fields) share a context
                 * and a kernel touching them never makes a cross-context access. Do
                 * not construct a Buffer over foreign USM.
                 *
                 * @param ctx          The runtime context that owns the allocation.
                 * @param count        Number of elements to allocate.
                 * @param device_index Target device queue; selects the owning context.
                 */
                Buffer(std::shared_ptr<Execution::RuntimeContext> ctx,
                       std::size_t count, std::size_t device_index = 0)
                    : ctx_(std::move(ctx)),
                      ptr_(static_cast<T*>(
                          ctx_->malloc_shared_on(count * sizeof(T), device_index))),
                      size_(count),
                      device_index_(device_index)
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
                      device_index_(other.device_index_)
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
                        device_index_ = other.device_index_;
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

                /** @brief Device queue index the allocation is pinned to. */
                std::size_t device_index() const noexcept { return device_index_; }

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

#ifdef SR_DISTRIBUTED
                /**
                 * @brief Registers this buffer as distributable (Master mode).
                 *
                 * Opt-in: only a registered buffer can cross to a worker, and the
                 * master stays authoritative for it. No-op when empty.
                 *
                 * Two overloads exist to make chaining safe. The lvalue overload
                 * (on a named buffer) returns *this. The rvalue overload (on a
                 * temporary returned by Runtime::buffer()) moves the buffer into
                 * the caller so the allocation survives the chain:
                 * `auto a = rt.buffer<float>(n).distributed();`
                 *
                 * Note: only a plain Buffer is distributable. A State<T> double-buffers
                 * by swapping pointers each step, which is incompatible with the
                 * version-by-pointer coherence model, so State deliberately has no
                 * distributed() — stage a Buffer instead.
                 *
                 * @return *this (lvalue overload) or the buffer by value (rvalue overload).
                 */
                Buffer& distributed() &
                {
                    if (ctx_ && ptr_)
                        ctx_->register_distributed_buffer(ptr_, size_ * sizeof(T));
                    return *this;
                }

                Buffer distributed() &&
                {
                    if (ctx_ && ptr_)
                        ctx_->register_distributed_buffer(ptr_, size_ * sizeof(T));
                    return std::move(*this);
                }
#endif // SR_DISTRIBUTED

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
                std::size_t device_index_ = 0;
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
                 * @brief Allocates both buffers of @p count elements on one device.
                 *
                 * Both fields land on @p device_index, so a step kernel that reads
                 * current and writes next stays within one context.
                 *
                 * @param ctx          The runtime context that owns the allocations.
                 * @param count        Number of elements in each buffer.
                 * @param device_index Target device queue for both buffers.
                 */
                State(std::shared_ptr<Execution::RuntimeContext> ctx,
                      std::size_t count, std::size_t device_index = 0)
                    : cur_(ctx, count, device_index),
                      next_(std::move(ctx), count, device_index)
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
