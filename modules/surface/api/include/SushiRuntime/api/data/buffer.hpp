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
 * @see modules/surface/api/include/SushiRuntime/api/data/README.md
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <sycl/sycl.hpp>
#include <SushiRuntime/api/detail/handle_access.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/logging/logger.hpp>
#include <SushiRuntime/memory/resource_liveness.hpp>
#include <SushiRuntime/memory/resource_region.hpp>
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
         * Its constructor is explicit, so a bare integer does not pass as a device index.
         */
        using DeviceIndex = Core::StrongID<struct DeviceIndexTag>;

        /**
         * @brief A half-open element range [offset, offset + count) into a handle.
         *
         * Names a window of a Buffer or State for the transfer and sub-region calls.
         * Counts are in elements, not bytes.
         */
        struct ElementRange
        {
            std::size_t offset = 0;  /**< First element of the range. */
            std::size_t count = 0;   /**< Number of elements in the range. */
        };

        /**
         * @brief Names where a Buffer or State keeps its memory across steps.
         * @see modules/surface/api/include/SushiRuntime/api/data/README.md
         */
        enum class Residency
        {
            Shared,  /**< Host-addressable USM; the default. */
            Device,  /**< Device-resident USM; host access only via read/write_range. */
            Host     /**< Pinned host USM; host-readable while kernels run. */
        };

        /** @brief Names the byte window of one allocation that region() returns. */
        using BufferRegion = Memory::ResourceRegion;

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
             * @brief Logs, the first time it is called, that distributed() found no offload
             *        coordinator.
             *
             * One flag serves every element type, so a program gets the line once.
             */
            inline void warn_distributed_without_coordinator()
            {
                static std::atomic_flag is_logged = ATOMIC_FLAG_INIT;
                if (is_logged.test_and_set(std::memory_order_relaxed))
                    return;

                SR_LOG_WARN("distributed() was called on a runtime with no offload coordinator, "
                            "so the buffer stays local and its tasks run on this machine; call "
                            "Cluster::attach() before distributed(). Later calls are not logged");
            }

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
                Memory::USMAllocator& memory = ctx.memory();
                switch (residency)
                {
                    case Residency::Device:
                        return memory.allocate({Memory::USMKind::Device, bytes, 0, device});
                    case Residency::Host:
                        return memory.allocate({Memory::USMKind::Host, bytes, 0, device});
                    case Residency::Shared:
                        return memory.allocate({Memory::USMKind::Shared, bytes, 0, device});
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

            /**
             * @brief Checks that a device index names one of the devices a context drives.
             * @param call         Names the calling operation in the message.
             * @param device_index Index to check.
             * @param device_count Number of devices the context drives.
             * @throws SushiException With invalid_access when @p device_index is not below
             *         @p device_count.
             */
            inline void require_device(const char* call, DeviceIndex device_index,
                                       std::size_t device_count)
            {
                if (device_index.value() < device_count)
                    return;
                throw Core::SushiException(Core::Error{
                    Core::Errc::invalid_access,
                    std::string(call) + ": device index " +
                    std::to_string(device_index.value()) +
                    " names no device; the runtime drives " + std::to_string(device_count) +
                    " devices, and advanced().devices() lists them by index."});
            }

            /**
             * @brief Returns the byte size of count elements.
             * @param call Names the calling operation in the message.
             * @throws SushiException With allocation_failed when the byte size does not fit
             *         std::size_t.
             */
            inline std::size_t checked_byte_count(const char* call, std::size_t count,
                                                  std::size_t element_size)
            {
                const std::size_t most = std::numeric_limits<std::size_t>::max() / element_size;
                if (count > most)
                    throw Core::SushiException(Core::Error{
                        Core::Errc::allocation_failed,
                        std::string(call) + ": " + std::to_string(count) + " elements of " +
                        std::to_string(element_size) + " bytes each exceed the largest byte "
                        "count; ask for at most " + std::to_string(most) + " elements."});
                return count * element_size;
            }

            /**
             * @brief Describes an allocation request for a message: elements, bytes, residency
             *        and device.
             */
            inline std::string describe_request(std::size_t count, std::size_t element_size,
                                                DeviceIndex device_index, Residency residency)
            {
                return std::to_string(count) + " elements of " + std::to_string(element_size) +
                       " bytes each (" + std::to_string(count * element_size) +
                       " bytes), residency " + residency_name(residency) + ", device " +
                       std::to_string(device_index.value());
            }

            /**
             * @brief Checks that an element window lies inside a handle.
             * @param call Names the calling operation in the message.
             * @param size Element count of the handle.
             * @throws SushiException With invalid_access when offset + count exceeds @p size.
             */
            inline void require_window(const char* call, ElementRange range, std::size_t size)
            {
                if (range.offset <= size && range.count <= size - range.offset)
                    return;
                throw Core::SushiException(Core::Error{
                    Core::Errc::invalid_access,
                    std::string(call) + ": the window (offset " + std::to_string(range.offset) +
                    ", count " + std::to_string(range.count) + ") does not fit a handle of size " +
                    std::to_string(size) + "; keep offset + count within size()."});
            }
        } // namespace detail

        /**
         * @brief Owns a typed block of unified shared memory that the runtime tracks by owning
         *        queue.
         * @tparam T Element type; must be trivially copyable, which the class checks at
         *         compile time.
         * @see modules/surface/api/include/SushiRuntime/api/data/README.md
         */
        template <typename T>
        class Buffer
        {
            public:
                static_assert(std::is_trivially_copyable<T>::value,
                              "Buffer<T>, and so State<T>, copies its elements as raw bytes, so T "
                              "must be trivially copyable: use a struct of plain members, such as "
                              "struct { float x; float y; }, in place of std::string or "
                              "std::vector.");

                /** @brief Element type, exposed so generic code can recover it. */
                using value_type = T;

                /** @brief Constructs an empty handle that owns no memory. */
                Buffer() = default;

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
                 * @warning A Buffer a graph recorded may be moved; destroying it or move-assigning
                 *        over it refuses the next run with dangling_resource.
                 * @see modules/surface/api/include/SushiRuntime/api/data/README.md
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
                [[nodiscard]] T* data() const noexcept
                {
                    return static_cast<T*>(allocation_.address());
                }

                /**
                 * @brief Reports the allocation's capacity.
                 * @return Number of elements, not bytes.
                 */
                [[nodiscard]] std::size_t size() const noexcept { return size_; }

                /**
                 * @brief Tells whether this handle owns no memory.
                 * @return True if it owns no allocation, e.g. after being moved from.
                 */
                [[nodiscard]] bool empty() const noexcept { return allocation_.empty(); }

                /**
                 * @brief Reports which device queue the allocation is pinned to.
                 * @return The device index the buffer was allocated on.
                 */
                [[nodiscard]] DeviceIndex device_index() const noexcept { return device_index_; }

                /**
                 * @brief Reports where the allocation lives.
                 * @return Residency::Shared for managed USM, Residency::Device for
                 *         device-resident memory, Residency::Host for pinned host USM.
                 */
                [[nodiscard]] Residency residency() const noexcept { return residency_; }

                /**
                 * @brief Tells whether the host may index this buffer directly.
                 * @return True for shared and host USM; false for device-resident
                 *         memory, which must go through read_range()/write_range().
                 */
                [[nodiscard]] bool is_host_accessible() const noexcept
                {
                    return residency_ != Residency::Device;
                }

                /**
                 * @brief Returns element i for host code; valid only for shared or host USM.
                 * @pre i < size().
                 * @throws SushiException If the buffer is device-resident.
                 * @see modules/surface/api/include/SushiRuntime/api/data/README.md
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
                 * @see modules/surface/api/include/SushiRuntime/api/data/README.md
                 */
                [[nodiscard]] span<T> host() const
                {
                    require_host_accessible("Buffer::host");
                    return span<T>(data(), size_);
                }

                /**
                 * @brief Copies a window out to a host vector through the owning queue and waits
                 *        for it.
                 * @param range Element window to read.
                 * @throws SushiException With invalid_access when @p range does not fit size().
                 * @see modules/surface/api/include/SushiRuntime/api/data/README.md
                 */
                [[nodiscard]] std::vector<T> read_range(ElementRange range) const
                {
                    return read_window("Buffer::read_range", range);
                }

                /**
                 * @brief Copies host elements into a window through the owning queue and waits.
                 * @param range  Element window to write.
                 * @param source Holds at least range.count elements; the first range.count are
                 *        copied.
                 * @throws SushiException With invalid_access when @p range does not fit size(), or
                 *        @p source is null or shorter than a non-empty window.
                 */
                void write_range(ElementRange range, span<const T> source) const
                {
                    write_window("Buffer::write_range", range, source);
                }

                /**
                 * @brief Names a window of this buffer so that tasks on disjoint windows are not
                 *        serialized.
                 * @param range Element window, in elements.
                 * @throws SushiException With invalid_access when @p range does not fit size().
                 * @see modules/surface/api/include/SushiRuntime/api/data/README.md
                 */
                [[nodiscard]] BufferRegion region(ElementRange range) const
                {
                    return region_of("Buffer::region", range);
                }

                /**
                 * @brief Shares this buffer with the runtime's offload coordinator; does nothing
                 *        when none is installed or the buffer is empty.
                 * @return *this.
                 * @throws SushiException With invalid_access when the buffer owns memory and is
                 *         device-resident.
                 * @see modules/surface/api/include/SushiRuntime/api/data/README.md
                 */
                Buffer& distributed() &
                {
                    share_with_offload("Buffer::distributed()");
                    return *this;
                }

                /**
                 * @brief Shares this buffer as distributed() does, then returns it by value.
                 * @return The buffer, moved out of this object.
                 * @throws SushiException With invalid_access when the buffer owns memory and is
                 *         device-resident.
                 * @see modules/surface/api/include/SushiRuntime/api/data/README.md
                 */
                [[nodiscard]] Buffer distributed() &&
                {
                    share_with_offload("Buffer::distributed()");
                    return std::move(*this);
                }

                /** @brief Forwards to is_host_accessible(). */
                [[deprecated("use is_host_accessible()")]]
                bool host_accessible() const noexcept
                {
                    return is_host_accessible();
                }

                /**
                 * @brief Forwards to write_range(ElementRange, span<const T>) with range.count
                 *        elements at @p src.
                 * @throws SushiException With invalid_access when @p src is null and range.count
                 *        is not zero.
                 */
                [[deprecated("use write_range(ElementRange, span<const T>)")]]
                void write_range(ElementRange range, const T* src) const
                {
                    write_window("Buffer::write_range", range, span<const T>(src, range.count));
                }

                /** @brief Forwards to detail::HandleAccess::key(). */
                [[deprecated("name the Buffer in Reads(...) or Writes(...) instead")]]
                void* dependency_key() const noexcept
                {
                    return detail::HandleAccess::key(*this);
                }

                /** @brief Forwards to detail::HandleAccess::liveness(). */
                [[deprecated("internal to the graph layer; declare the access with Reads, Writes, "
                             "In, Out or InOut")]]
                Memory::LivenessObserver liveness() const noexcept
                {
                    return detail::HandleAccess::liveness(*this);
                }

            private:
                friend class Runtime;
                friend class Interop;
                template <typename>
                friend class State;
                friend struct detail::HandleAccess;

                /**
                 * @brief Allocates count elements of USM pinned to one device.
                 * @param call Names the public operation in a thrown message.
                 * @throws SushiException With invalid_access for an unknown device index, or
                 *         allocation_failed when the byte size overflows or the device returns
                 *         nothing.
                 * @see modules/surface/api/include/SushiRuntime/api/data/README.md
                 */
                Buffer(const char* call, std::shared_ptr<Execution::RuntimeContext> ctx,
                       std::size_t count, DeviceIndex device_index, Residency residency)
                    : ctx_(std::move(ctx)),
                      size_(count),
                      device_index_(device_index),
                      residency_(residency),
                      liveness_(Memory::make_liveness())
                {
                    detail::require_device(call, device_index, ctx_->devices().count());
                    const std::size_t bytes =
                        detail::checked_byte_count("Buffer allocation", count, sizeof(T));
                    auto owner =
                        std::make_unique<Memory::USMAllocationOwner>(ctx_->shared_memory());
                    void* address =
                        detail::malloc_residency_on(*ctx_, bytes, device_index, residency);
                    // A null address would give the handle a null key, which skips tracking.
                    if (count > 0 && address == nullptr)
                        throw Core::SushiException(Core::Error{
                            Core::Errc::allocation_failed,
                            "Buffer allocation: the device returned no memory for " +
                            detail::describe_request(count, sizeof(T), device_index, residency) +
                            "."});
                    allocation_ = Memory::Allocation(address, bytes, std::move(owner));
                }

                /**
                 * @brief Wraps memory another component allocated and will release.
                 * @param allocation Owns at least count * sizeof(T) bytes; released when this
                 *        buffer is.
                 * @throws SushiException With allocation_failed when the byte size of @p count
                 *        overflows or @p allocation is smaller than it; @p allocation is released
                 *        before the exception propagates.
                 */
                Buffer(std::shared_ptr<Execution::RuntimeContext> ctx,
                       Memory::Allocation allocation, std::size_t count,
                       DeviceIndex device_index, Residency residency)
                    : ctx_(std::move(ctx)),
                      allocation_(std::move(allocation)),
                      size_(count),
                      device_index_(device_index),
                      residency_(residency),
                      liveness_(Memory::make_liveness())
                {
                    const std::size_t bytes =
                        detail::checked_byte_count("Buffer adoption", count, sizeof(T));
                    if (allocation_.bytes() < bytes)
                        throw Core::SushiException(Core::Error{
                            Core::Errc::allocation_failed,
                            "Buffer adoption: an allocation of " +
                            std::to_string(allocation_.bytes()) + " bytes cannot hold " +
                            detail::describe_request(count, sizeof(T), device_index, residency) +
                            "."});
                }

                /**
                 * @brief Copies a window out to a host vector and counts the bytes moved.
                 * @param call Names the public operation in a thrown message.
                 * @throws SushiException With invalid_access when @p range does not fit size().
                 */
                std::vector<T> read_window(const char* call, ElementRange range) const
                {
                    detail::require_window(call, range, size_);
                    std::vector<T> out(range.count);
                    if (range.count == 0)
                        return out;
                    // A non-zero size_ implies a context and a non-null address.
                    T* first = data();
                    sycl::queue& queue = detail::owning_queue(*ctx_, static_cast<void*>(first));
                    queue.memcpy(out.data(), first + range.offset, range.count * sizeof(T)).wait();
                    ctx_->transfers().add_device_to_host(range.count * sizeof(T));
                    return out;
                }

                /**
                 * @brief Copies host elements into a window and counts the bytes moved.
                 * @param call Names the public operation in a thrown message.
                 * @throws SushiException With invalid_access when @p range does not fit size(), or
                 *        @p source is null or shorter than a non-empty window.
                 */
                void write_window(const char* call, ElementRange range, span<const T> source) const
                {
                    detail::require_window(call, range, size_);
                    if (range.count == 0)
                        return;
                    if (source.data() == nullptr)
                        throw Core::SushiException(Core::Error{
                            Core::Errc::invalid_access,
                            std::string(call) + ": the source is null for a window of count " +
                            std::to_string(range.count) + "; pass that many elements."});
                    if (source.size() < range.count)
                        throw Core::SushiException(Core::Error{
                            Core::Errc::invalid_access,
                            std::string(call) + ": the source holds " +
                            std::to_string(source.size()) + " elements for a window of count " +
                            std::to_string(range.count) + "; pass at least that many."});
                    // A non-zero size_ implies a context and a non-null address.
                    T* first = data();
                    sycl::queue& queue = detail::owning_queue(*ctx_, static_cast<void*>(first));
                    queue.memcpy(first + range.offset, source.data(), range.count * sizeof(T))
                        .wait();
                    ctx_->transfers().add_host_to_device(range.count * sizeof(T));
                }

                /**
                 * @brief Returns the byte window of this allocation that an element window names.
                 * @param call Names the public operation in a thrown message.
                 * @throws SushiException With invalid_access when @p range does not fit size().
                 */
                BufferRegion region_of(const char* call, ElementRange range) const
                {
                    detail::require_window(call, range, size_);
                    return BufferRegion{static_cast<void*>(data()),
                                        range.offset * sizeof(T),
                                        range.count * sizeof(T)};
                }

                /**
                 * @brief Hands the allocation to the installed offload coordinator, or warns.
                 * @param where Names the public call in a thrown message.
                 * @throws SushiException With invalid_access when the handle owns memory and is
                 *         not host-accessible.
                 */
                void share_with_offload(const char* where)
                {
                    if (!ctx_ || !data())
                        return;
                    require_shareable(where);
                    if (auto* offload = ctx_->offload())
                        offload->share(data(), size_ * sizeof(T));
                    else
                        detail::warn_distributed_without_coordinator();
                }

                /**
                 * @brief Throws an invalid_access error when the offload coordinator cannot read
                 *        the allocation on the host.
                 * @param where Names the public call in the message.
                 */
                void require_shareable(const char* where) const
                {
                    if (is_host_accessible())
                        return;
                    throw Core::SushiException(Core::Error{
                        Core::Errc::invalid_access,
                        std::string(where) + ": the handle has " + residency_name(residency_) +
                        " residency, and the host cannot address it; allocate the handle with a "
                        "host-accessible residency, Residency::Shared or Residency::Host."});
                }

                /** @brief Throws an invalid_access error when host indexing is invalid. */
                void require_host_accessible(const char* where) const
                {
                    if (!is_host_accessible())
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
                Memory::LivenessToken liveness_;
        };

    } // namespace API

    using API::Buffer;
    using API::Residency;
    using API::DeviceIndex;
    using API::ElementRange;
    using API::BufferRegion;

} // namespace SushiRuntime
