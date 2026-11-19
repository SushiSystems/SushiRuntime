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
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
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

#include <SushiRuntime/api/data/alias_storage.hpp>
#include <SushiRuntime/api/data/buffer_storage.hpp>
#include <SushiRuntime/api/data/planned_storage.hpp>
#include <SushiRuntime/api/data/request_checks.hpp>
#include <SushiRuntime/api/data/residency.hpp>
#include <SushiRuntime/api/data/stated_storage.hpp>
#include <SushiRuntime/api/detail/handle_access.hpp>
#include <SushiRuntime/context/context_handle.hpp>
#include <SushiRuntime/core/allocation.hpp>
#include <SushiRuntime/core/assert.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/error_translation.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/strong_id.hpp>
#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/region/buffer_region.hpp>
#include <SushiRuntime/region/liveness.hpp>

namespace SushiRuntime
{
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

    namespace Detail
    {
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
         * @brief Mints the handle of a buffer the runtime made and records its home instance.
         * @param address The buffer's memory on its device; null mints nothing.
         * @param bytes,element_size,element_alignment Size and element layout of the buffer.
         * @param residency The residency the memory was allocated with, recorded as its kind.
         * @return The handle, the null handle for a null address, or an AllocationFailed
         *         error when no slot is left.
         */
        [[nodiscard]] SUSHIRUNTIME_API Result<GlobalHandle> mint_handle(
            const ContextHandle& context, HandleKind kind, void* address, std::size_t bytes,
            std::size_t element_size, std::size_t element_alignment, DeviceIndex device,
            Residency residency);

        /**
         * @brief Returns the Borrowed handle of caller-owned memory: the live one when the same
         *        range is borrowed already, else a new one.
         * @param device Device the memory is recorded on: its handle's home space.
         * @param residency The kind of memory, when the caller checked it; none leaves it unknown.
         * @return The handle, the null handle for a null address or no bytes, or an
         *         InvalidArgument error when the range overlaps another borrowed range.
         */
        [[nodiscard]] SUSHIRUNTIME_API Result<GlobalHandle> borrow_handle(
            const ContextHandle& context, void* address, std::size_t bytes,
            std::size_t element_size, std::size_t element_alignment, DeviceIndex device,
            std::optional<Residency> residency);

        /** @brief Retires a minted handle, or drops one borrow of a Borrowed handle. */
        SUSHIRUNTIME_API void release_handle(const ContextHandle& context,
                                             GlobalHandle handle) noexcept;

        /**
         * @brief Mints the handle of a Residency::Planned buffer: its size and layout, no
         *        instance on any space.
         * @return The handle, the null handle for no bytes, or an AllocationFailed error when
         *         no slot is left.
         */
        [[nodiscard]] SUSHIRUNTIME_API Result<GlobalHandle> mint_planned_handle(
            const ContextHandle& context, HandleKind kind, std::size_t bytes,
            std::size_t element_size, std::size_t element_alignment);

        /**
         * @brief Mints the handle of a Residency::Planned buffer whose bytes are a file range:
         *        the range's read-only space holds the one valid instance.
         * @return The handle, the null handle for no bytes, an InvalidArgument error when the
         *         file is shorter than the range, or an AllocationFailed error when no handle or
         *         space id is left.
         */
        [[nodiscard]] SUSHIRUNTIME_API Result<GlobalHandle> mint_file_handle(
            const ContextHandle& context, HandleKind kind, const std::string& path,
            std::uint64_t offset, std::size_t bytes, std::size_t element_size,
            std::size_t element_alignment);

        /**
         * @brief Copies bytes of a planned buffer to host memory from a space that holds them.
         * @param begin First byte of the buffer to read.
         * @return Nothing, or the error the directory, a transfer path or the host space failed
         *         with.
         */
        [[nodiscard]] SUSHIRUNTIME_API Result<void> read_planned(const ContextHandle& context,
                                                                 GlobalHandle handle,
                                                                 std::size_t begin,
                                                                 void* destination,
                                                                 std::size_t bytes) noexcept;

        /**
         * @brief Writes host bytes into a planned buffer's host instance and marks every other
         *        space stale for them.
         * @param begin First byte of the buffer to write.
         * @return Nothing, or the error the directory or the host space failed with.
         */
        [[nodiscard]] SUSHIRUNTIME_API Result<void> write_planned(const ContextHandle& context,
                                                                  GlobalHandle handle,
                                                                  std::size_t begin,
                                                                  const void* source,
                                                                  std::size_t bytes) noexcept;

        /** @brief Frees every instance of a planned buffer and retires its handle. */
        SUSHIRUNTIME_API void release_planned_handle(const ContextHandle& context,
                                                     GlobalHandle handle) noexcept;

        /**
         * @brief Copies bytes of a buffer to host memory through its device's queue, waits,
         *        and counts them as moved device to host.
         * @param device The buffer's device; source lies inside the buffer.
         * @return Nothing, or a KernelFailed error when the device fails.
         */
        [[nodiscard]] SUSHIRUNTIME_API Result<void> copy_to_host(const ContextHandle& context,
                                                                 DeviceIndex device,
                                                                 const void* source,
                                                                 void* destination,
                                                                 std::size_t bytes) noexcept;

        /**
         * @brief Copies host bytes into a buffer through its device's queue, waits, and
         *        counts them as moved host to device.
         * @param device The buffer's device; destination lies inside the buffer.
         * @return Nothing, or a KernelFailed error when the device fails.
         */
        [[nodiscard]] SUSHIRUNTIME_API Result<void> copy_to_device(const ContextHandle& context,
                                                                   DeviceIndex device,
                                                                   void* destination,
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
            SR_VERIFY(names_a_device(device_index.value(), device_count),
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
            SR_VERIFY(is_valid_alignment(alignment),
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
            SR_VERIFY(fits_byte_count(count, element_size),
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
         * @brief Returns the InvalidArgument error of a residency other than Planned asked for
         *        with no device.
         * @param call Names the calling operation in the message.
         */
        inline Error residency_needs_device(const char* call, Residency residency)
        {
            return Error{ErrorCode::InvalidArgument,
                         std::string(call) + ": Residency::" + residency_name(residency) +
                             " keeps its memory on a stated device; pass a DeviceIndex with it, "
                             "or pass Residency::Planned alone."};
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

            /** @brief Frees the owned allocation, if any; borrowed memory stays with its owner. */
            ~Buffer()
            {
                reset();
            }

            Buffer(const Buffer&) = delete;
            Buffer& operator=(const Buffer&) = delete;

            /**
             * @brief Takes other's allocation, or the memory it borrows, and its liveness,
             *        leaving other empty.
             * @warning A Buffer a graph recorded may be moved; destroying it or move-assigning
             *        over it refuses the next run with DanglingResource.
             * @see modules/surface/api/include/SushiRuntime/api/data/README.md
             */
            Buffer(Buffer&& other) noexcept
                : context_(std::move(other.context_)),
                  storage_(std::move(other.storage_)),
                  size_(other.size_),
                  liveness_(std::move(other.liveness_)),
                  handle_(other.handle_)
            {
                other.size_ = 0;
                other.handle_ = GlobalHandle::null();
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
                    storage_ = std::move(other.storage_);
                    size_ = other.size_;
                    liveness_ = std::move(other.liveness_);
                    handle_ = other.handle_;
                    other.handle_ = GlobalHandle::null();
                    other.size_ = 0;
                    other.liveness_.reset();
                }
                return *this;
            }

            /**
             * @brief Exposes the raw pointer a kernel captures: the address of the buffer's
             *        instance on its home device.
             * @return Pointer to the first element; null if the handle is empty, and null for a
             *         Residency::Planned buffer, whose instances the runtime may move.
             */
            [[nodiscard]] T* data() const noexcept
            {
                return static_cast<T*>(storage_address());
            }

            /**
             * @brief Returns the identity of the buffer, the key every graph orders it by.
             * @return The buffer's GlobalHandle, or the null handle for an empty buffer.
             * @see modules/surface/api/include/SushiRuntime/api/data/README.md
             */
            [[nodiscard]] GlobalHandle handle() const noexcept { return handle_; }

            /**
             * @brief Reports the allocation's capacity.
             * @return Number of elements, not bytes.
             */
            [[nodiscard]] std::size_t size() const noexcept { return size_; }

            /**
             * @brief Tells whether this handle owns no memory.
             * @return True if it owns no allocation, e.g. after being moved from.
             */
            [[nodiscard]] bool empty() const noexcept
            {
                return storage_address() == nullptr && handle_.is_null();
            }

            /**
             * @brief Reports which device queue the allocation is pinned to.
             * @return The device index the buffer was allocated on; empty for a
             *         Residency::Planned buffer, which states no device, and for an empty one.
             */
            [[nodiscard]] std::optional<DeviceIndex> device_index() const noexcept
            {
                if (!storage_)
                    return std::nullopt;
                return storage_->device_index();
            }

            /**
             * @brief Reports where the allocation lives.
             * @return Residency::Shared for managed USM, Residency::Device for
             *         device-resident memory, Residency::Host for pinned host USM,
             *         Residency::Planned for memory the runtime places.
             */
            [[nodiscard]] Residency residency() const noexcept
            {
                if (!storage_)
                    return Residency::Shared;
                return storage_->residency();
            }

            /**
             * @brief Tells whether the host may index this buffer directly.
             * @return True for shared and host USM; false for device-resident and planned
             *         memory, which must go through read_range()/write_range().
             */
            [[nodiscard]] bool is_host_accessible() const noexcept
            {
                return !storage_ || storage_->host_addressable();
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

            /**
             * @brief Views this buffer's memory as bytes, under this buffer's handle, owning
             *        nothing; a graph orders the view and this buffer as one resource.
             * @return The view; an empty one for an empty buffer; InvalidArgument for a
             *         Residency::Planned buffer, which has no address.
             * @warning A graph that recorded the view refuses its next run with DanglingResource
             *          once this buffer is gone.
             */
            [[nodiscard]] Result<Buffer<std::byte>> as_bytes() const;

        private:
            template <typename>
            friend class Buffer;
            friend class Runtime;
            friend class Interop;
            friend class PlacementScope;
            template <typename>
            friend class State;
            friend struct Detail::HandleAccess;

            /**
             * @brief Allocates count elements of USM pinned to one device.
             * @param call Names the public operation in a message.
             * @param kind The kind of handle minted: Buffer, or State for a State's field.
             * @return The buffer, or an AllocationFailed error when the device returns nothing.
             * @pre device_index names a device, count's bytes fit, alignment is 0 or a power of 2.
             * @see modules/surface/api/include/SushiRuntime/api/data/README.md
             */
            [[nodiscard]] static Result<Buffer> allocate(
                const char* call, const ContextHandle& context,
                std::size_t count, DeviceIndex device_index, Residency residency,
                std::size_t alignment, HandleKind kind = HandleKind::Buffer)
            {
                if (residency == Residency::Planned)
                    return make_error(Error{
                        ErrorCode::InvalidArgument,
                        std::string(call) + ": Residency::Planned takes no device, because the "
                        "runtime places a planned buffer; leave the device out and pass only "
                        "the count and Residency::Planned."});
                Detail::require_device(call, device_index, Detail::device_count(context));
                Detail::require_alignment(call, alignment);
                static_cast<void>(
                    Detail::checked_byte_count("Buffer allocation", count, sizeof(T)));

                Result<Detail::MemoryBlock> block = Detail::allocate_block(
                    context, count, sizeof(T), device_index.value(), residency, alignment);
                if (!block)
                    return make_error(std::move(block.error()));
                return adopt(context, std::move(*block), count, device_index, residency, kind);
            }

            /**
             * @brief Makes a Residency::Planned buffer: a handle with a size and no memory.
             * @param call Names the public operation in a message.
             * @param kind The kind of handle minted: Buffer, or State for a State's field.
             * @return The buffer, or an AllocationFailed error when no handle is left.
             * @pre count's bytes fit.
             * @see modules/surface/api/include/SushiRuntime/api/data/README.md
             */
            [[nodiscard]] static Result<Buffer> plan(const char* call,
                                                     const ContextHandle& context,
                                                     std::size_t count,
                                                     HandleKind kind = HandleKind::Buffer)
            {
                const std::size_t bytes = Detail::checked_byte_count(call, count, sizeof(T));
                Result<GlobalHandle> handle = Detail::mint_planned_handle(
                    context, kind, bytes, sizeof(T), alignof(T));
                if (!handle)
                    return make_error(std::move(handle.error()));
                std::optional<Buffer> made;
                Result<void> built = Detail::translate_exceptions([&]()
                {
                    made.emplace(Buffer(context, count, *handle));
                });
                if (!built)
                {
                    Detail::release_planned_handle(context, *handle);
                    return make_error(std::move(built.error()));
                }
                return std::move(*made);
            }

            /**
             * @brief Makes a Residency::Planned buffer of count elements of a file, from byte
             *        offset; the file is read only when a space needs the bytes.
             * @param call Names the public operation in a message.
             * @return The buffer, InvalidArgument for a short file, or AllocationFailed.
             * @pre count's bytes fit.
             */
            [[nodiscard]] static Result<Buffer> plan_from_file(const char* call,
                                                               const ContextHandle& context,
                                                               const std::string& path,
                                                               std::uint64_t offset,
                                                               std::size_t count)
            {
                const std::size_t bytes = Detail::checked_byte_count(call, count, sizeof(T));
                Result<GlobalHandle> handle = Detail::mint_file_handle(
                    context, HandleKind::Buffer, path, offset, bytes, sizeof(T), alignof(T));
                if (!handle)
                    return make_error(std::move(handle.error()));
                std::optional<Buffer> made;
                Result<void> built = Detail::translate_exceptions([&]()
                {
                    made.emplace(Buffer(context, count, *handle));
                });
                if (!built)
                {
                    Detail::release_planned_handle(context, *handle);
                    return make_error(std::move(built.error()));
                }
                return std::move(*made);
            }

            /**
             * @brief Wraps memory another component allocated and will release; mints its handle.
             * @param block Holds count * sizeof(T) bytes or more; released here on a failure.
             * @param kind The kind of handle minted.
             * @return The buffer, or AllocationFailed when its token or handle cannot be made.
             * @pre The byte size of count fits std::size_t and block holds at least it.
             */
            [[nodiscard]] static Result<Buffer> adopt(
                const ContextHandle& context, Detail::MemoryBlock block, std::size_t count,
                DeviceIndex device_index, Residency residency,
                HandleKind kind = HandleKind::Buffer) noexcept
            {
                Result<GlobalHandle> handle = Detail::mint_handle(
                    context, kind, block.allocation.address(), block.allocation.bytes(),
                    sizeof(T), alignof(T), device_index, residency);
                if (!handle)
                    return make_error(std::move(handle.error()));
                std::optional<Buffer> made;
                Result<void> built = Detail::translate_exceptions([&]()
                {
                    made.emplace(Buffer(context, std::move(block), count, device_index,
                                        residency, *handle));
                });
                if (!built)
                {
                    Detail::release_handle(context, *handle);
                    return make_error(std::move(built.error()));
                }
                return std::move(*made);
            }

            /**
             * @brief Views memory another owner keeps alive; Interop::borrow() is its one
             *        caller.
             * @param owner_life Expires when the owner releases the memory.
             * @param handle The Borrowed handle, or null; a failure drops one borrow of it.
             * @return The buffer, or an AllocationFailed error when its storage cannot be made.
             * @pre block holds at least count elements and its allocation has no owner.
             */
            [[nodiscard]] static Result<Buffer> view(
                const ContextHandle& context, Detail::MemoryBlock block, std::size_t count,
                DeviceIndex device_index, Residency residency, LivenessObserver owner_life,
                GlobalHandle handle) noexcept
            {
                std::optional<Buffer> made;
                Result<void> built = Detail::translate_exceptions([&]()
                {
                    made.emplace(Buffer(context, std::move(block), count, device_index,
                                        residency, std::move(owner_life), handle));
                });
                if (!built)
                {
                    Detail::release_handle(context, handle);
                    return make_error(std::move(built.error()));
                }
                return std::move(*made);
            }

            /**
             * @brief Takes memory another component allocated and the handle minted for it;
             *        adopt() is its one caller.
             * @param handle Released by this buffer when it has a context.
             * @pre The byte size of count fits std::size_t and block holds at least it.
             */
            Buffer(ContextHandle context, Detail::MemoryBlock block, std::size_t count,
                   DeviceIndex device_index, Residency residency, GlobalHandle handle)
                : context_(std::move(context)),
                  size_(count),
                  liveness_(make_liveness()),
                  handle_(handle)
            {
                const std::size_t bytes =
                    Detail::checked_byte_count("Buffer adoption", count, sizeof(T));
                SR_VERIFY(block.allocation.bytes() >= bytes,
                          ("Buffer adoption: an allocation of " +
                           std::to_string(block.allocation.bytes()) + " bytes cannot hold " +
                           Detail::describe_request(count, sizeof(T), device_index, residency) +
                           ".")
                              .c_str());
                storage_ = std::make_unique<Detail::StatedStorage>(std::move(block), device_index,
                                                                   residency);
            }

            /**
             * @brief Takes the handle of a planned buffer, which holds no memory; plan() is its
             *        one caller.
             * @param handle Released, with every instance the directory records for it, by this
             *        buffer when it has a context.
             */
            Buffer(ContextHandle context, std::size_t count, GlobalHandle handle)
                : context_(std::move(context)),
                  storage_(std::make_unique<Detail::PlannedStorage>()),
                  size_(count),
                  liveness_(make_liveness()),
                  handle_(handle)
            {

            }

            /**
             * @brief Views memory another owner keeps alive; Interop::borrow() is its one
             *        caller.
             * @param owner_life Expires when the owner releases the memory.
             * @param handle The Borrowed handle of the memory; one borrow of it is dropped by
             *        this buffer when it has a context.
             * @pre block holds at least count elements and its allocation has no owner.
             */
            Buffer(ContextHandle context, Detail::MemoryBlock block, std::size_t count,
                   DeviceIndex device_index, Residency residency,
                   LivenessObserver owner_life, GlobalHandle handle)
                : context_(std::move(context)),
                  storage_(std::make_unique<Detail::StatedStorage>(
                      std::move(block), device_index, residency, std::move(owner_life))),
                  size_(count),
                  handle_(handle)
            {

            }

            /**
             * @brief Views memory another live buffer keeps under that buffer's handle;
             *        as_bytes() is its one caller.
             * @param storage Releases nothing, so the handle stays with the viewed buffer.
             */
            Buffer(ContextHandle context, std::unique_ptr<Detail::AliasStorage> storage,
                   std::size_t count, GlobalHandle handle) noexcept
                : context_(std::move(context)),
                  storage_(std::move(storage)),
                  size_(count),
                  handle_(handle)
            {

            }

            /**
             * @brief Returns the observer a graph keeps for this handle.
             * @return The owned token's observer, the owner's for borrowed memory, or an
             *         empty one for an empty handle.
             */
            [[nodiscard]] LivenessObserver observer() const noexcept
            {
                if (liveness_)
                    return liveness_;
                if (storage_)
                    return storage_->owner_life();
                return LivenessObserver();
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
                // A non-zero size_ implies a runtime, and a non-null address or a planned handle.
                if (copied && range.count != 0)
                    copied = storage_->read(context_, handle_, range.offset * sizeof(T),
                                            out.data(), range.count * sizeof(T));
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
                // A non-zero size_ implies a runtime, and a non-null address or a planned handle.
                return storage_->write(context_, handle_, range.offset * sizeof(T), source.data(),
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
                return BufferRegion{handle_, range.offset * sizeof(T), range.count * sizeof(T)};
            }

            /**
             * @brief Stops the process unless the host may address the allocation.
             * @param where Names the public call in the message.
             */
            void require_host_accessible(const char* where) const
            {
                SR_VERIFY(is_host_accessible(),
                          (std::string(where) +
                           ": " + storage_->host_refusal() +
                           "use read_range() and write_range().")
                              .c_str());
            }

            /** @brief Frees the owned allocation, releases its handle and clears the buffer. */
            void reset() noexcept
            {
                if (storage_ && context_ && !handle_.is_null())
                    storage_->release(context_, handle_);
                handle_ = GlobalHandle::null();
                storage_.reset();
                size_ = 0;
                // Resetting the token expires every graph's observer of this allocation.
                liveness_.reset();
            }

            /** @brief Returns the address of the storage's memory, or null for none. */
            [[nodiscard]] void* storage_address() const noexcept
            {
                return storage_ ? storage_->address() : nullptr;
            }

            /** @brief The runtime the memory came from; empty for an empty handle. */
            ContextHandle context_;

            /** @brief The memory and the way to reach it; null for an empty handle. */
            std::unique_ptr<Detail::BufferStorage> storage_;

            std::size_t size_ = 0;
            LivenessToken liveness_;

            /** @brief Identity of the buffer; null for an empty buffer. */
            GlobalHandle handle_;
    };

    template <typename T>
    Result<Buffer<std::byte>> Buffer<T>::as_bytes() const
    {
        if (empty())
            return Buffer<std::byte>();
        if (residency() == Residency::Planned)
            return make_error(Error{
                ErrorCode::InvalidArgument,
                "Buffer::as_bytes: a Residency::Planned buffer has no address to view, because "
                "the runtime places its instances; view a buffer of a stated device."});
        std::unique_ptr<Detail::AliasStorage> storage;
        Result<void> built = Detail::translate_exceptions([&]()
        {
            storage = std::make_unique<Detail::AliasStorage>(
                storage_address(), size_ * sizeof(T), *device_index(), residency(), observer());
        });
        if (!built)
            return make_error(std::move(built.error()));
        return Buffer<std::byte>(context_, std::move(storage), size_ * sizeof(T), handle_);
    }
} // namespace SushiRuntime
