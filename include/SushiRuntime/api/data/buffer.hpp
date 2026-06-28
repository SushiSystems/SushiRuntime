/**************************************************************************/
/* buffer.hpp                                                              */
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

#include <cassert>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include <sycl/sycl.hpp>
#include <SushiRuntime/core/resource_region.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>

namespace SushiRuntime
{
    namespace API
    {
        /**
         * @brief Where a Buffer/State keeps its memory across steps.
         *
         * Shared USM is host-addressable, so the host may seed and read it directly,
         * but at large element counts the driver may migrate it between host and
         * device. Device USM stays resident on its device — the right choice for a
         * world kept on the GPU across frames — but is not host-addressable, so the
         * host touches it only through read_range()/write_range(). The handle API is
         * otherwise identical; residency is chosen once at allocation.
         */
        enum class Residency
        {
            Shared,  /**< Host-addressable USM; the default. */
            Device   /**< Device-resident USM; host access only via read/write_range. */
        };

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
                /** @brief Element type, exposed so generic code can recover it. */
                using value_type = T;

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
                 * @param residency    Shared (host-addressable) or Device (resident).
                 */
                Buffer(std::shared_ptr<Execution::RuntimeContext> ctx,
                       std::size_t count, std::size_t device_index = 0,
                       Residency residency = Residency::Shared)
                    : ctx_(std::move(ctx)),
                      ptr_(static_cast<T*>(
                          residency == Residency::Device
                              ? ctx_->malloc_device_on(count * sizeof(T), device_index)
                              : ctx_->malloc_shared_on(count * sizeof(T), device_index))),
                      size_(count),
                      device_index_(device_index),
                      residency_(residency)
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
                      device_index_(other.device_index_),
                      residency_(other.residency_)
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
                        residency_ = other.residency_;
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

                /** @brief Where the allocation lives (shared or device-resident). */
                Residency residency() const noexcept { return residency_; }

                /** @brief True if the host may index this buffer directly. */
                bool host_accessible() const noexcept { return residency_ == Residency::Shared; }

                /**
                 * @brief Host-side element access; valid only for shared USM.
                 *
                 * Device-resident memory is not host-addressable: dereferencing it on
                 * the host is undefined, so this asserts host_accessible() in debug
                 * builds. For device residency use read_range()/write_range() instead.
                 */
                T& operator[](std::size_t i) const noexcept
                {
                    assert(host_accessible() &&
                           "device-resident buffer is not host-indexable; use read_range/write_range");
                    return ptr_[i];
                }

                /**
                 * @brief Copies a sub-range out to the host, for any residency.
                 *
                 * The selective-readback seam: rather than host-mapping a whole
                 * device-resident field, copy just [offset, offset+count) through the
                 * owning queue into a host vector — telemetry, IO, or a debug window.
                 * Synchronous: the copy is waited on before returning. The range is
                 * clamped to what the buffer holds.
                 *
                 * @param offset First element to read.
                 * @param count  Number of elements to read; clamped to the remainder.
                 * @return A host vector holding the copied elements.
                 */
                std::vector<T> read_range(std::size_t offset, std::size_t count) const
                {
                    const std::size_t avail = (offset < size_) ? (size_ - offset) : 0;
                    if (count > avail) count = avail;
                    std::vector<T> out(count);
                    if (count && ptr_ && ctx_)
                    {
                        auto& q = ctx_->get_queue_for_ptr(static_cast<void*>(ptr_));
                        q.memcpy(out.data(), ptr_ + offset, count * sizeof(T)).wait();
                        ctx_->add_device_to_host_bytes(count * sizeof(T));
                    }
                    return out;
                }

                /**
                 * @brief Copies a host range into a sub-range, for any residency.
                 *
                 * The counterpart to read_range and the way to seed a device-resident
                 * buffer the host cannot index. Synchronous: the copy is waited on
                 * before returning. The range is clamped to what the buffer holds.
                 *
                 * @param offset First element to write.
                 * @param src    Host source of at least @p count elements.
                 * @param count  Number of elements to write; clamped to the remainder.
                 */
                void write_range(std::size_t offset, const T* src, std::size_t count)
                {
                    const std::size_t avail = (offset < size_) ? (size_ - offset) : 0;
                    if (count > avail) count = avail;
                    if (count && ptr_ && ctx_ && src)
                    {
                        auto& q = ctx_->get_queue_for_ptr(static_cast<void*>(ptr_));
                        q.memcpy(ptr_ + offset, src, count * sizeof(T)).wait();
                        ctx_->add_host_to_device_bytes(count * sizeof(T));
                    }
                }

                /**
                 * @brief The dependency-tracking identity of this buffer.
                 *
                 * Graph::add() passes this to the dependency tracker as the
                 * read/write key. It is the allocation's base address, which the
                 * tracker treats as the whole-buffer region.
                 *
                 * @return The base pointer as a void*, or nullptr when empty.
                 */
                void* dependency_key() const noexcept { return static_cast<void*>(ptr_); }

                /**
                 * @brief Names a sub-range of this buffer for fine-grained ordering.
                 *
                 * Two tasks declaring regions of the same buffer are serialized only
                 * when their ranges overlap, so disjoint slices (an array's halves, a
                 * grid's interior versus its halo) run in parallel. Pass the result to
                 * Reads(...)/Writes(...) in place of the buffer itself. Offsets are in
                 * elements; the buffer keeps the whole-buffer default when used bare.
                 *
                 * @param offset First element of the range.
                 * @param count  Number of elements; clamped to what remains after @p offset.
                 * @return A region keyed on this allocation, covering [offset, offset+count).
                 */
                Core::ResourceRegion region(std::size_t offset, std::size_t count) const noexcept
                {
                    const std::size_t avail = (offset < size_) ? (size_ - offset) : 0;
                    if (count > avail) count = avail;
                    return Core::ResourceRegion{static_cast<void*>(ptr_),
                                                offset * sizeof(T),
                                                count * sizeof(T)};
                }

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
                 * A State<T> is also distributable (State::distributed()); it
                 * registers both of its allocations and the offload path resolves the
                 * step's read/write handles from the live pointers, so the swap is
                 * followed without breaking the version-by-pointer coherence model.
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
                Residency residency_ = Residency::Shared;
        };

    } // namespace API

    using API::Buffer;
    using API::Residency;

} // namespace SushiRuntime
