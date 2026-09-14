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

#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <sycl/sycl.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/resource_liveness.hpp>
#include <SushiRuntime/core/resource_region.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/strong_id.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/memory/allocation.hpp>
#include <SushiRuntime/memory/usm_allocation_owner.hpp>

namespace SushiRuntime
{
    namespace API
    {
        /**
         * @brief Which device queue an allocation is pinned to.
         *
         * A strong typedef so a device selector cannot be confused with an element
         * count or an offset at a call site — buffer(count, DeviceIndex{1}) reads
         * unambiguously and a swapped argument is a compile error, not a silent
         * allocation on the wrong device.
         */
        using DeviceIndex = Core::StrongId<struct DeviceIndexTag>;

        /**
         * @brief A half-open element range [offset, offset + count) into a handle.
         *
         * Names a window of a Buffer or State for the transfer and sub-region calls.
         * Bundling the two counts into one named value keeps a caller from passing
         * them in the wrong order — the failure a pair of bare std::size_t arguments
         * invites. Counts are in elements, not bytes.
         */
        struct ElementRange
        {
            std::size_t offset = 0;  /**< First element of the range. */
            std::size_t count = 0;   /**< Number of elements in the range. */
        };

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
                       std::size_t count, DeviceIndex device_index = DeviceIndex{0},
                       Residency residency = Residency::Shared)
                    : ctx_(std::move(ctx)),
                      size_(count),
                      device_index_(device_index),
                      residency_(residency),
                      liveness_(Core::make_liveness())
                {
                    const std::size_t bytes = count * sizeof(T);
                    auto owner = std::make_unique<Memory::UsmAllocationOwner>(ctx_);
                    void* address = residency == Residency::Device
                                        ? ctx_->malloc_device_on(bytes, device_index.value())
                                        : ctx_->malloc_shared_on(bytes, device_index.value());
                    // A null address here would make dependency_key() null and skip tracking.
                    if (count > 0 && address == nullptr)
                        throw Core::SushiException(Core::Error{
                            Core::Errc::allocation_failed,
                            "Buffer allocation of " + std::to_string(bytes) +
                            " bytes failed (out of USM memory)."});
                    allocation_ = Memory::Allocation(address, bytes, std::move(owner));
                }

                /**
                 * @brief Wraps memory another component allocated and will release.
                 * @param ctx          The runtime context this buffer is pinned to.
                 * @param allocation   Owns at least count * sizeof(T) bytes; released
                 *                     when this buffer is.
                 * @param count        Number of elements @p allocation covers.
                 * @param device_index Target device queue.
                 * @param residency    Shared (host-addressable) or Device (resident).
                 * @throws SushiException With allocation_failed when @p count overflows
                 *         its byte count, or @p allocation is smaller than it; @p allocation
                 *         is released before the exception propagates.
                 */
                Buffer(std::shared_ptr<Execution::RuntimeContext> ctx, Memory::Allocation allocation,
                       std::size_t count, DeviceIndex device_index, Residency residency)
                    : ctx_(std::move(ctx)),
                      allocation_(std::move(allocation)),
                      size_(count),
                      device_index_(device_index),
                      residency_(residency),
                      liveness_(Core::make_liveness())
                {
                    if (count > std::numeric_limits<std::size_t>::max() / sizeof(T) ||
                        allocation_.bytes() < count * sizeof(T))
                        throw Core::SushiException(Core::Error{
                            Core::Errc::allocation_failed,
                            "Buffer adoption of " + std::to_string(count) +
                            " elements over an allocation of " +
                            std::to_string(allocation_.bytes()) + " bytes."});
                }

                /** @brief Frees the owned allocation, if any. */
                ~Buffer()
                {
                    reset();
                }

                Buffer(const Buffer&) = delete;
                Buffer& operator=(const Buffer&) = delete;

                /**
                 * @brief Takes ownership of @p other's allocation.
                 *
                 * The liveness token moves WITH the allocation, by design: replay and
                 * factory patterns (`auto s = rt.state<T>(n)`, returning a handle,
                 * moving one into place before recording it) rely on the token
                 * following the value.
                 *
                 * @warning A fluent graph records a handle by its object ADDRESS
                 * (State kernels capture `&state` and dereference it each step). Once
                 * a Buffer/State has been passed to `Graph::add(...)`, do NOT move it
                 * (or store it in a container that may reallocate, e.g. push_back into
                 * a `std::vector<State>` after recording an element): the graph keeps
                 * pointing at the old address while the object lives at the new one,
                 * and — because the token travels with the value — the liveness guard
                 * cannot detect it. Keep recorded handles fixed for the graph's life.
                 *
                 * @param other Buffer to take the allocation from; left empty.
                 */
                Buffer(Buffer&& other) noexcept
                    : ctx_(std::move(other.ctx_)),
                      allocation_(std::move(other.allocation_)),
                      size_(other.size_),
                      device_index_(other.device_index_),
                      residency_(other.residency_),
                      liveness_(std::move(other.liveness_))
                {
                    other.size_ = 0;
                }

                /**
                 * @brief Frees this handle's memory, then takes @p other's.
                 * @param other Buffer to take the allocation from; left empty.
                 * @return *this, now owning @p other's allocation.
                 */
                Buffer& operator=(Buffer&& other) noexcept
                {
                    if (this != &other)
                    {
                        reset();
                        ctx_ = std::move(other.ctx_);
                        allocation_ = std::move(other.allocation_);
                        size_ = other.size_;
                        device_index_ = other.device_index_;
                        residency_ = other.residency_;
                        liveness_ = std::move(other.liveness_);
                        other.size_ = 0;
                        other.liveness_.reset();
                    }
                    return *this;
                }

                /**
                 * @brief Exposes the raw pointer a kernel captures.
                 * @return Pointer to the first element, or null if the handle is empty.
                 */
                T* data() const noexcept { return static_cast<T*>(allocation_.address()); }

                /**
                 * @brief Reports the allocation's capacity.
                 * @return Number of elements, not bytes.
                 */
                std::size_t size() const noexcept { return size_; }

                /**
                 * @brief Tells whether this handle still owns memory.
                 * @return True if it owns no allocation, e.g. after being moved from.
                 */
                bool empty() const noexcept { return allocation_.empty(); }

                /**
                 * @brief Reports which device queue the allocation is pinned to.
                 * @return The device index the buffer was allocated on.
                 */
                DeviceIndex device_index() const noexcept { return device_index_; }

                /**
                 * @brief Reports where the allocation lives.
                 * @return Residency::Shared for host-addressable USM,
                 *         Residency::Device for device-resident memory.
                 */
                Residency residency() const noexcept { return residency_; }

                /**
                 * @brief Tells whether the host may index this buffer directly.
                 * @return True for shared USM; false for device-resident memory, which
                 *         must go through read_range()/write_range().
                 */
                bool host_accessible() const noexcept { return residency_ == Residency::Shared; }

                /**
                 * @brief Host-side element access; valid only for shared USM.
                 *
                 * Device-resident memory is not host-addressable, so indexing it on the
                 * host is invalid. Rather than dereference into undefined behaviour, this
                 * throws on a device-resident buffer — a deterministic failure in every
                 * build, not just a debug assert. For device residency use
                 * read_range()/write_range(), or host() for a bulk host view. Kernels
                 * capture data() directly and never go through this operator, so the
                 * residency check costs nothing on the device path.
                 *
                 * @param i Element index to access.
                 * @return A reference to element @p i.
                 * @throws SushiException If the buffer is device-resident.
                 */
                T& operator[](std::size_t i) const
                {
                    require_host_accessible("Buffer::operator[]");
                    return data()[i];
                }

                /**
                 * @brief A host-addressable view over the whole buffer.
                 *
                 * The branch-free bulk seam for host code: the residency is checked once,
                 * here, and the returned span indexes with no further checks — seed or
                 * read back in a tight loop without paying per-element validation. Valid
                 * only for shared USM; a device-resident buffer throws, since its memory
                 * is not host-addressable (use read_range()/write_range() there).
                 *
                 * @return A span over all elements, valid while this buffer lives.
                 * @throws SushiException If the buffer is device-resident.
                 */
                span<T> host() const
                {
                    require_host_accessible("Buffer::host");
                    return span<T>(data(), size_);
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
                 * @param range Element window to read; count is clamped to the remainder.
                 * @return A host vector holding the copied elements.
                 */
                std::vector<T> read_range(ElementRange range) const
                {
                    const std::size_t offset = range.offset;
                    std::size_t count = range.count;
                    const std::size_t avail = (offset < size_) ? (size_ - offset) : 0;
                    if (count > avail) count = avail;
                    std::vector<T> out(count);
                    T* ptr = data();
                    if (count && ptr && ctx_)
                    {
                        auto& q = ctx_->get_queue_for_ptr(static_cast<void*>(ptr));
                        q.memcpy(out.data(), ptr + offset, count * sizeof(T)).wait();
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
                 * @param range Element window to write; count is clamped to the remainder.
                 * @param src   Host source of at least range.count elements.
                 */
                void write_range(ElementRange range, const T* src)
                {
                    const std::size_t offset = range.offset;
                    std::size_t count = range.count;
                    const std::size_t avail = (offset < size_) ? (size_ - offset) : 0;
                    if (count > avail) count = avail;
                    T* ptr = data();
                    if (count && ptr && ctx_ && src)
                    {
                        auto& q = ctx_->get_queue_for_ptr(static_cast<void*>(ptr));
                        q.memcpy(ptr + offset, src, count * sizeof(T)).wait();
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
                void* dependency_key() const noexcept { return static_cast<void*>(data()); }

                /**
                 * @brief A weak observer of this buffer's allocation lifetime.
                 *
                 * A graph that records this buffer keeps the returned observer; it stays
                 * valid while the buffer lives and expires when the buffer is destroyed
                 * or moved-from. That lets a run detect a buffer freed before it ran and
                 * fail cleanly instead of dispatching against released memory. Empty for
                 * a handle that owns no allocation.
                 *
                 * @return A weak observer of the allocation, empty when this handle is empty.
                 */
                Core::LivenessObserver liveness() const noexcept { return liveness_; }

                /**
                 * @brief Names a sub-range of this buffer for fine-grained ordering.
                 *
                 * Two tasks declaring regions of the same buffer are serialized only
                 * when their ranges overlap, so disjoint slices (an array's halves, a
                 * grid's interior versus its halo) run in parallel. Pass the result to
                 * Reads(...)/Writes(...) in place of the buffer itself. Offsets are in
                 * elements; the buffer keeps the whole-buffer default when used bare.
                 *
                 * @param range Element window to name; count is clamped to the remainder.
                 * @return A region keyed on this allocation, covering the element window.
                 */
                Core::ResourceRegion region(ElementRange range) const noexcept
                {
                    const std::size_t offset = range.offset;
                    std::size_t count = range.count;
                    const std::size_t avail = (offset < size_) ? (size_ - offset) : 0;
                    if (count > avail) count = avail;
                    return Core::ResourceRegion{static_cast<void*>(data()),
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
                    if (ctx_ && data())
                        ctx_->register_distributed_buffer(data(), size_ * sizeof(T));
                    return *this;
                }

                Buffer distributed() &&
                {
                    if (ctx_ && data())
                        ctx_->register_distributed_buffer(data(), size_ * sizeof(T));
                    return std::move(*this);
                }
#endif // SR_DISTRIBUTED

            private:
                /** @brief Throws an invalid_access error when host indexing is invalid. */
                void require_host_accessible(const char* where) const
                {
                    if (!host_accessible())
                        throw Core::SushiException(Core::Error{
                            Core::Errc::invalid_access,
                            std::string(where) +
                            ": device-resident buffer is not host-addressable; "
                            "use read_range()/write_range()."});
                }

                /** @brief Frees the owned allocation and clears the handle. */
                void reset() noexcept
                {
                    allocation_.reset();
                    size_ = 0;
                    // Drop the token so any graph observing this allocation sees it
                    // expire — the signal that the buffer no longer backs its pointer.
                    liveness_.reset();
                }

                std::shared_ptr<Execution::RuntimeContext> ctx_;
                Memory::Allocation allocation_;
                std::size_t size_ = 0;
                DeviceIndex device_index_ = DeviceIndex{0};
                Residency residency_ = Residency::Shared;
                Core::LivenessToken liveness_;
        };

    } // namespace API

    using API::Buffer;
    using API::Residency;
    using API::DeviceIndex;
    using API::ElementRange;

} // namespace SushiRuntime
