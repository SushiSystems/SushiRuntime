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

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/api/detail/handle_access.hpp>
#include <SushiRuntime/context/context_handle.hpp>
#include <SushiRuntime/core/assert.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/error_translation.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/strong_id.hpp>
#include <SushiRuntime/region/buffer_region.hpp>
#include <SushiRuntime/region/liveness.hpp>

namespace SushiRuntime
{
    /**
     * @brief Which device queue an allocation is pinned to.
     *
     * Its constructor is explicit, so a bare integer does not pass as a device index.
     */
    using DeviceIndex = StrongID<struct DeviceIndexTag>;

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
    enum class Residency : std::uint8_t
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

    namespace Detail
    {
        /**
         * @brief Names one allocation and what keeps it alive, with nothing of how it was
         *        made.
         * @see modules/surface/api/include/SushiRuntime/api/data/README.md
         */
        struct MemoryBlock
        {
            void* address = nullptr;     /**< Base address, or null for no memory. */
            std::size_t bytes = 0;       /**< Bytes the block holds. */
            std::shared_ptr<void> owner; /**< Returns the memory when the last copy goes. */
        };

        /** @brief Returns how many devices a runtime drives. */
        [[nodiscard]] SUSHIRUNTIME_API std::size_t device_count(const ContextHandle& context);

        /**
         * @brief Allocates USM of one residency on one device, owned by the block returned.
         * @param alignment Byte alignment of the address; 0 asks for the device default.
         * @return The block, or an AllocationFailed error when the device returns nothing
         *         for a count above zero.
         * @pre The byte size of count elements fits std::size_t.
         */
        [[nodiscard]] SUSHIRUNTIME_API Result<MemoryBlock> allocate_block(
            const ContextHandle& context, std::size_t count, std::size_t element_size,
            std::size_t device, Residency residency, std::size_t alignment);

        /**
         * @brief Copies bytes of an allocation to host memory through its owning queue,
         *        waits, and counts them as moved device to host.
         * @param base Base address of the allocation; source lies inside it.
         * @return Nothing, or a KernelFailed error when the device fails.
         */
        [[nodiscard]] SUSHIRUNTIME_API Result<void> copy_to_host(const ContextHandle& context,
                                                                 void* base, const void* source,
                                                                 void* destination,
                                                                 std::size_t bytes) noexcept;

        /**
         * @brief Copies host bytes into an allocation through its owning queue, waits, and
         *        counts them as moved host to device.
         * @param base Base address of the allocation; destination lies inside it.
         * @return Nothing, or a KernelFailed error when the device fails.
         */
        [[nodiscard]] SUSHIRUNTIME_API Result<void> copy_to_device(const ContextHandle& context,
                                                                   void* base, void* destination,
                                                                   const void* source,
                                                                   std::size_t bytes) noexcept;

        /**
         * @brief Stops the process unless a device index names a device the context drives.
         * @param call Names the calling operation in the message.
         * @pre device_index is below device_count.
         */
        inline void require_device(const char* call, DeviceIndex device_index,
                                   std::size_t device_count)
        {
            SR_VERIFY(device_index.value() < device_count,
                      (std::string(call) + ": device index " +
                       std::to_string(device_index.value()) +
                       " names no device; the runtime drives " + std::to_string(device_count) +
                       " devices, and advanced().devices() lists them by index.")
                          .c_str());
        }

        /**
         * @brief Stops the process unless an alignment is 0 or a power of two.
         * @param call Names the calling operation in the message.
         * @pre alignment is 0 or a power of two.
         */
        inline void require_alignment(const char* call, std::size_t alignment)
        {
            SR_VERIFY((alignment & (alignment - 1)) == 0,
                      (std::string(call) + ": alignment " + std::to_string(alignment) +
                       " is not a power of two; pass 0 for the device default or a power of "
                       "two in bytes.")
                          .c_str());
        }

        /**
         * @brief Returns the byte size of count elements.
         * @param call Names the calling operation in the message.
         * @pre The byte size fits std::size_t.
         */
        inline std::size_t checked_byte_count(const char* call, std::size_t count,
                                              std::size_t element_size)
        {
            const std::size_t most = std::numeric_limits<std::size_t>::max() / element_size;
            SR_VERIFY(count <= most,
                      (std::string(call) + ": " + std::to_string(count) + " elements of " +
                       std::to_string(element_size) + " bytes each exceed the largest byte "
                       "count; ask for at most " + std::to_string(most) + " elements.")
                          .c_str());
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
         * @brief Stops the process unless an element window lies inside a handle.
         * @param call Names the calling operation in the message.
         * @param size Element count of the handle.
         * @pre range.offset + range.count is at most size.
         */
        inline void require_window(const char* call, ElementRange range, std::size_t size)
        {
            SR_VERIFY(range.offset <= size && range.count <= size - range.offset,
                      (std::string(call) + ": the window (offset " +
                       std::to_string(range.offset) + ", count " + std::to_string(range.count) +
                       ") does not fit a handle of size " + std::to_string(size) +
                       "; keep offset + count within size().")
                          .c_str());
        }
    } // namespace Detail

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
             *        over it refuses the next run with DanglingResource.
             * @see modules/surface/api/include/SushiRuntime/api/data/README.md
             */
            Buffer(Buffer&& other) noexcept
                : context_(std::move(other.context_)),
                  block_(std::exchange(other.block_, {})),
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
                    context_ = std::move(other.context_);
                    block_ = std::exchange(other.block_, {});
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
                return static_cast<T*>(block_.address);
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
            [[nodiscard]] bool empty() const noexcept { return block_.address == nullptr; }

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
             * @brief Returns element i for host code.
             * @pre i < size(), and the buffer is shared or host USM.
             * @see modules/surface/api/include/SushiRuntime/api/data/README.md
             */
            T& operator[](std::size_t i) const
            {
                SR_ASSERT(is_host_accessible(),
                          "Buffer::operator[]: a device-resident buffer is not host-addressable; "
                          "use read_range() and write_range()");
                return data()[i];
            }

            /**
             * @brief Returns a host-addressable span over the whole buffer after one residency
             *        check.
             * @return A span over all elements, valid while this buffer lives.
             * @pre The buffer is shared or host USM.
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
             * @return The elements, or a KernelFailed error when the device fails the copy.
             * @pre range fits size().
             * @see modules/surface/api/include/SushiRuntime/api/data/README.md
             */
            [[nodiscard]] Result<std::vector<T>> read_range(ElementRange range) const
            {
                return read_window("Buffer::read_range", range);
            }

            /**
             * @brief Copies host elements into a window through the owning queue and waits.
             * @param range  Element window to write.
             * @param source Holds at least range.count elements; the first range.count are
             *        copied.
             * @return Nothing, or a KernelFailed error when the device fails the copy.
             * @pre range fits size(), and source holds at least range.count elements.
             */
            [[nodiscard]] Result<void> write_range(ElementRange range, span<const T> source) const
            {
                return write_window("Buffer::write_range", range, source);
            }

            /**
             * @brief Names a window of this buffer so that tasks on disjoint windows are not
             *        serialized.
             * @param range Element window, in elements.
             * @pre range fits size().
             * @see modules/surface/api/include/SushiRuntime/api/data/README.md
             */
            [[nodiscard]] BufferRegion region(ElementRange range) const
            {
                return region_of("Buffer::region", range);
            }

        private:
            friend class Runtime;
            friend class Interop;
            friend class PlacementScope;
            template <typename>
            friend class State;
            friend struct Detail::HandleAccess;

            /**
             * @brief Allocates count elements of USM pinned to one device.
             * @param call Names the public operation in a message.
             * @return The buffer, or an AllocationFailed error when the device returns nothing.
             * @pre device_index names a device, the byte size of count fits std::size_t, and
             *      alignment is 0 or a power of two.
             * @see modules/surface/api/include/SushiRuntime/api/data/README.md
             */
            [[nodiscard]] static Result<Buffer> allocate(
                const char* call, const ContextHandle& context,
                std::size_t count, DeviceIndex device_index, Residency residency,
                std::size_t alignment)
            {
                Detail::require_device(call, device_index, Detail::device_count(context));
                Detail::require_alignment(call, alignment);
                static_cast<void>(
                    Detail::checked_byte_count("Buffer allocation", count, sizeof(T)));

                Result<Detail::MemoryBlock> block = Detail::allocate_block(
                    context, count, sizeof(T), device_index.value(), residency, alignment);
                if (!block)
                    return make_error(std::move(block.error()));
                return adopt(context, std::move(*block), count, device_index, residency);
            }

            /**
             * @brief Wraps memory another component allocated and will release.
             * @param block Holds at least count * sizeof(T) bytes; its owner is let go when
             *        the buffer is, or here on a failure.
             * @return The buffer, or an AllocationFailed error when its liveness token
             *         cannot be allocated.
             * @pre The byte size of count fits std::size_t and block holds at least it.
             */
            [[nodiscard]] static Result<Buffer> adopt(
                const ContextHandle& context, Detail::MemoryBlock block, std::size_t count,
                DeviceIndex device_index, Residency residency) noexcept
            {
                std::optional<Buffer> made;
                Result<void> built = Detail::translate_exceptions([&]()
                {
                    made.emplace(
                        Buffer(context, std::move(block), count, device_index, residency));
                });
                if (!built)
                    return make_error(std::move(built.error()));
                return std::move(*made);
            }

            /**
             * @brief Takes memory another component allocated; adopt() is its one caller.
             * @pre The byte size of count fits std::size_t and block holds at least it.
             */
            Buffer(ContextHandle context, Detail::MemoryBlock block, std::size_t count,
                   DeviceIndex device_index, Residency residency)
                : context_(std::move(context)),
                  block_(std::move(block)),
                  size_(count),
                  device_index_(device_index),
                  residency_(residency),
                  liveness_(make_liveness())
            {
                const std::size_t bytes =
                    Detail::checked_byte_count("Buffer adoption", count, sizeof(T));
                SR_VERIFY(block_.bytes >= bytes,
                          ("Buffer adoption: an allocation of " +
                           std::to_string(block_.bytes) + " bytes cannot hold " +
                           Detail::describe_request(count, sizeof(T), device_index, residency) +
                           ".")
                              .c_str());
            }

            /**
             * @brief Copies a window out to a host vector and counts the bytes moved.
             * @param call Names the public operation in a message.
             * @return The elements, or a KernelFailed error when the device fails the copy.
             * @pre range fits size().
             */
            [[nodiscard]] Result<std::vector<T>> read_window(const char* call,
                                                             ElementRange range) const
            {
                Detail::require_window(call, range, size_);
                std::vector<T> out;
                Result<void> copied = Detail::translate_exceptions([&]()
                {
                    out.resize(range.count);
                });
                // A non-zero size_ implies a runtime and a non-null address.
                if (copied && range.count != 0)
                    copied = Detail::copy_to_host(context_, static_cast<void*>(data()),
                                                  data() + range.offset, out.data(),
                                                  range.count * sizeof(T));
                if (!copied)
                    return make_error(std::move(copied.error()));
                return std::move(out);
            }

            /**
             * @brief Copies host elements into a window and counts the bytes moved.
             * @param call Names the public operation in a message.
             * @return Nothing, or a KernelFailed error when the device fails the copy.
             * @pre range fits size(), and source holds at least range.count elements.
             */
            [[nodiscard]] Result<void> write_window(const char* call, ElementRange range,
                                                    span<const T> source) const
            {
                Detail::require_window(call, range, size_);
                if (range.count == 0)
                    return {};
                SR_VERIFY(source.data() != nullptr,
                          (std::string(call) + ": the source is null for a window of count " +
                           std::to_string(range.count) + "; pass that many elements.")
                              .c_str());
                SR_VERIFY(source.size() >= range.count,
                          (std::string(call) + ": the source holds " +
                           std::to_string(source.size()) + " elements for a window of count " +
                           std::to_string(range.count) + "; pass at least that many.")
                              .c_str());
                // A non-zero size_ implies a runtime and a non-null address.
                return Detail::copy_to_device(context_, static_cast<void*>(data()),
                                              data() + range.offset, source.data(),
                                              range.count * sizeof(T));
            }

            /**
             * @brief Returns the byte window of this allocation that an element window names.
             * @param call Names the public operation in a message.
             * @pre range fits size().
             */
            BufferRegion region_of(const char* call, ElementRange range) const
            {
                Detail::require_window(call, range, size_);
                return BufferRegion{static_cast<void*>(data()),
                                    range.offset * sizeof(T),
                                    range.count * sizeof(T)};
            }

            /**
             * @brief Stops the process unless the host may address the allocation.
             * @param where Names the public call in the message.
             */
            void require_host_accessible(const char* where) const
            {
                SR_VERIFY(is_host_accessible(),
                          (std::string(where) +
                           ": a device-resident buffer is not host-addressable; "
                           "use read_range() and write_range().")
                              .c_str());
            }

            /** @brief Frees the owned allocation and clears the handle. */
            void reset() noexcept
            {
                block_ = {};
                size_ = 0;
                // Resetting the token expires every graph's observer of this allocation.
                liveness_.reset();
            }

            /** @brief The runtime the memory came from; empty for an empty handle. */
            ContextHandle context_;

            /** @brief The allocation and what keeps it alive. */
            Detail::MemoryBlock block_;

            std::size_t size_ = 0;
            DeviceIndex device_index_ = DeviceIndex{0};
            Residency residency_ = Residency::Shared;
            LivenessToken liveness_;
    };
} // namespace SushiRuntime
