/**************************************************************************/
/* buffer.hpp                                                             */
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
 * @file buffer.hpp
 * @brief Declares Buffer, the owning handle to a typed block of unified shared memory.
 * @author Mustafa Garip
 * @see include/SushiRuntime/api/data/README.md
 */

#pragma once

#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <sycl/sycl.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/resource_liveness.hpp>
#include <SushiRuntime/core/resource_region.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/strong_id.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/memory/allocation.hpp>
#include <SushiRuntime/memory/usm_allocation_owner.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/memory/usm_request.hpp>
#include <SushiRuntime/observability/transfer_counters.hpp>
#include <SushiRuntime/task/offload_coordinator.hpp>

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
         * @brief Names where a Buffer or State keeps its memory across steps.
         * @see include/SushiRuntime/api/data/README.md
         */
        enum class Residency
        {
            Shared,  /**< Host-addressable USM; the default. */
            Device,  /**< Device-resident USM; host access only via read/write_range. */
            Host     /**< Pinned host USM; host-readable while kernels run. */
        };

        /**
         * @brief Names a residency as its enumerator spelling, for messages.
         * @param residency The residency to name.
         * @return "Shared", "Device", "Host", or "unknown" for an out-of-range value.
         */
        inline const char* residency_name(Residency residency) noexcept
        {
            switch (residency)
            {
                case Residency::Shared: return "Shared";
                case Residency::Device: return "Device";
                case Residency::Host:   return "Host";
            }
            return "unknown";
        }

        namespace detail
        {
            /**
             * @brief Allocates USM of the given residency on one device queue.
             * @param ctx          Runtime context that performs and records the allocation.
             * @param bytes        Allocation size in bytes.
             * @param device_index Device queue that owns the allocation.
             * @param residency    Kind of USM to allocate.
             * @return The USM pointer, or nullptr on failure.
             */
            inline void* malloc_residency_on(Execution::RuntimeContext& ctx, std::size_t bytes,
                                             DeviceIndex device_index, Residency residency)
            {
                const std::size_t device = device_index.value();
                Memory::UsmAllocator& memory = ctx.memory();
                switch (residency)
                {
                    case Residency::Device:
                        return memory.allocate({Memory::UsmKind::Device, bytes, 0, device});
                    case Residency::Host:
                        return memory.allocate({Memory::UsmKind::Host, bytes, 0, device});
                    case Residency::Shared:
                        return memory.allocate({Memory::UsmKind::Shared, bytes, 0, device});
                }
                return nullptr;
            }

            /**
             * @brief Returns the queue of the device that owns an address, or a queue of
             *        NUMA node 0 when no device of the context owns it.
             */
            inline sycl::queue& owning_queue(Execution::RuntimeContext& ctx, void* address)
            {
                const std::optional<std::size_t> device = ctx.memory().owning_device(address);
                return ctx.devices().queue_or_numa_local(device, 0);
            }
        } // namespace detail

        /**
         * @brief Owns a typed block of unified shared memory that the runtime tracks by owning
         *        queue.
         * @tparam T Element type; must be trivially copyable for device use.
         * @see include/SushiRuntime/api/data/README.md
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
                 * @brief Allocates count elements of USM pinned to one device and records the
                 *        owner.
                 * @see include/SushiRuntime/api/data/README.md
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
                    auto owner = std::make_unique<Memory::UsmAllocationOwner>(ctx_->shared_memory());
                    void* address = detail::malloc_residency_on(*ctx_, bytes, device_index, residency);
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
                 * @param allocation   Owns at least count * sizeof(T) bytes; released
                 *        when this buffer is.
                 * @throws SushiException With allocation_failed when @p count overflows
                 *        its byte count, or @p allocation is smaller than it; @p allocation
                 *        is released before the exception propagates.
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
                 * @brief Takes ownership of other's allocation and liveness token, leaving other
                 *        empty.
                 * @warning Do not move a handle after Graph::add() recorded it; the graph keeps its
                 *        old address.
                 * @see include/SushiRuntime/api/data/README.md
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
                 * @return Residency::Shared for managed USM, Residency::Device for
                 *         device-resident memory, Residency::Host for pinned host USM.
                 */
                Residency residency() const noexcept { return residency_; }

                /**
                 * @brief Tells whether the host may index this buffer directly.
                 * @return True for shared and host USM; false for device-resident
                 *         memory, which must go through read_range()/write_range().
                 */
                bool host_accessible() const noexcept { return residency_ != Residency::Device; }

                /**
                 * @brief Returns element i for host code; valid only for shared or host USM.
                 * @throws SushiException If the buffer is device-resident.
                 * @see include/SushiRuntime/api/data/README.md
                 */
                T& operator[](std::size_t i) const
                {
                    require_host_accessible("Buffer::operator[]");
                    return data()[i];
                }

                /**
                 * @brief Returns a host-addressable span over the whole buffer after one residency
                 *        check.
                 * @return A span over all elements, valid while this buffer lives.
                 * @throws SushiException If the buffer is device-resident.
                 * @see include/SushiRuntime/api/data/README.md
                 */
                span<T> host() const
                {
                    require_host_accessible("Buffer::host");
                    return span<T>(data(), size_);
                }

                /**
                 * @brief Copies a sub-range out to a host vector through the owning queue and waits
                 *        for it.
                 * @param range Element window to read; count is clamped to the remainder.
                 * @see include/SushiRuntime/api/data/README.md
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
                        auto& q = detail::owning_queue(*ctx_, static_cast<void*>(ptr));
                        q.memcpy(out.data(), ptr + offset, count * sizeof(T)).wait();
                        ctx_->transfers().add_device_to_host(count * sizeof(T));
                    }
                    return out;
                }

                /**
                 * @brief Copies a host range into a sub-range through the owning queue and waits
                 *        for it.
                 * @param range Element window to write; count is clamped to the remainder.
                 * @param src Host source of at least range.count elements.
                 * @see include/SushiRuntime/api/data/README.md
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
                        auto& q = detail::owning_queue(*ctx_, static_cast<void*>(ptr));
                        q.memcpy(ptr + offset, src, count * sizeof(T)).wait();
                        ctx_->transfers().add_host_to_device(count * sizeof(T));
                    }
                }

                /**
                 * @brief Returns the allocation's base address, which the dependency tracker uses
                 *        as the key.
                 * @return The base pointer as a void*, or nullptr when empty.
                 * @see include/SushiRuntime/api/data/README.md
                 */
                void* dependency_key() const noexcept { return static_cast<void*>(data()); }

                /**
                 * @brief Returns a weak observer that expires when this buffer is destroyed or
                 *        moved from.
                 * @see include/SushiRuntime/api/data/README.md
                 */
                Core::LivenessObserver liveness() const noexcept { return liveness_; }

                /**
                 * @brief Names a sub-range of this buffer so that tasks on disjoint ranges are not
                 *        serialized.
                 * @param range Element window, in elements; count is clamped to the remainder.
                 * @see include/SushiRuntime/api/data/README.md
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

                /**
                 * @brief Shares this buffer with the runtime's offload coordinator; does nothing
                 *        when none is installed or the buffer is empty.
                 * @return *this from the lvalue overload, the buffer by value from the rvalue
                 *        overload.
                 * @see include/SushiRuntime/api/data/README.md
                 */
                Buffer& distributed() &
                {
                    share_with_offload();
                    return *this;
                }

                Buffer distributed() &&
                {
                    share_with_offload();
                    return std::move(*this);
                }

            private:
                /** @brief Hands the allocation to the installed offload coordinator, if any. */
                void share_with_offload()
                {
                    if (!ctx_ || !data())
                        return;
                    if (auto* offload = ctx_->offload())
                        offload->share(data(), size_ * sizeof(T));
                }

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
                    // Resetting the token expires every graph's observer of this allocation.
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
