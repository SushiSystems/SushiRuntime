/**************************************************************************/
/* interop.cpp                                                            */
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
 * @file interop.cpp
 * @brief Defines the non-template entry points behind the Interop facade.
 * @author Sushi (mustafagarip@sushisystems.io)
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <string>
#include <utility>

#include <sycl/sycl.hpp>

#include <SushiRuntime/core/assert.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include "bindless_detection.hpp"
#include <SushiRuntime/interop/interop.hpp>
#include <SushiRuntime/logging/logger.hpp>
#include <SushiRuntime/memory/usm_allocation_owner.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/memory/usm_request.hpp>

#if defined(SYCL_EXT_ONEAPI_BINDLESS_IMAGES)
#include <sycl/ext/oneapi/bindless_images.hpp>
#endif

namespace SushiRuntime
{
    namespace
    {
#if defined(SYCL_EXT_ONEAPI_BINDLESS_IMAGES)
        namespace Experimental = sycl::ext::oneapi::experimental;
#endif

        /// @brief Rounds @p bytes up to a multiple of @p alignment.
        std::size_t round_up(std::size_t bytes, std::size_t alignment) noexcept
        {
            return (bytes + alignment - 1) / alignment * alignment;
        }

        /// @brief Tells whether @p value is a power of two.
        bool is_power_of_two(std::size_t value) noexcept
        {
            return value != 0 && (value & (value - 1)) == 0;
        }

        /// @brief Reports capabilities for a device queue already looked up by its caller.
        InteropCapabilities capabilities_for(Internal::Execution::RuntimeContext& context, sycl::queue& queue,
                                             std::size_t device_index)
        {
            sycl::device device = queue.get_device();

            InteropCapabilities capabilities;
            capabilities.host_export = device.has(sycl::aspect::usm_host_allocations);
            if (device.has(sycl::aspect::ext_intel_device_info_uuid))
            {
                DeviceUUID uuid;
                uuid.bytes = device.get_info<sycl::ext::intel::info::device::uuid>();
                capabilities.device_uuid = uuid;
            }
            capabilities.device_import = Detail::K_BINDLESS_IMAGES_AVAILABLE
                && context.devices().is_gpu(device_index)
                && device.has(sycl::aspect::ext_oneapi_external_memory_import)
                && capabilities.device_uuid.has_value();
            return capabilities;
        }

#if defined(SYCL_EXT_ONEAPI_BINDLESS_IMAGES)
#if defined(_WIN32)
        /// @brief Names the handle kind this platform's importer accepts.
        constexpr ExternalHandleKind K_PLATFORM_HANDLE_KIND = ExternalHandleKind::OpaqueWin32;
#else
        /// @brief Names the handle kind this platform's importer accepts.
        constexpr ExternalHandleKind K_PLATFORM_HANDLE_KIND = ExternalHandleKind::OpaqueFd;
#endif

        /// @brief Returns the enumerator name of @p kind, or "unknown" for any other value.
        const char* kind_name(ExternalHandleKind kind) noexcept
        {
            switch (kind)
            {
                case ExternalHandleKind::Unspecified: return "Unspecified";
                case ExternalHandleKind::OpaqueWin32: return "OpaqueWin32";
                case ExternalHandleKind::OpaqueFd:    return "OpaqueFd";
            }
            return "unknown";
        }

        /// @brief Spells @p uuid as thirty-two lowercase hexadecimal digits.
        std::string hex(const DeviceUUID& uuid)
        {
            static constexpr char K_DIGITS[] = "0123456789abcdef";
            std::string text;
            text.reserve(uuid.bytes.size() * 2);
            for (const std::uint8_t byte : uuid.bytes)
            {
                text += K_DIGITS[byte >> 4];
                text += K_DIGITS[byte & 0x0F];
            }
            return text;
        }

        /// @brief Builds the exception import_device throws for a descriptor it rejects.
        SushiException descriptor_error(std::string detail)
        {
            return SushiException(Error{
                ErrorCode::InvalidAccess, "import_device: " + std::move(detail)});
        }

        /**
         * @brief Throws unless @p descriptor can describe @p bytes on this platform.
         * @throws SushiException With InvalidAccess, naming the field at fault.
         */
        void require_importable(const ExternalMemoryDescriptor& descriptor, std::size_t bytes)
        {
            if (descriptor.kind == ExternalHandleKind::Unspecified)
                throw descriptor_error(
                    std::string("descriptor.kind is Unspecified; the exporter must state ") +
                    "it, and this platform imports " + kind_name(K_PLATFORM_HANDLE_KIND) +
                    " handles.");

            if (descriptor.kind != K_PLATFORM_HANDLE_KIND)
                throw descriptor_error(
                    std::string("descriptor.kind is ") + kind_name(descriptor.kind) +
                    " (value " + std::to_string(static_cast<unsigned>(descriptor.kind)) +
                    ") and this platform imports " + kind_name(K_PLATFORM_HANDLE_KIND) +
                    " handles only.");

            if (descriptor.kind == ExternalHandleKind::OpaqueWin32 && descriptor.handle == 0)
                throw descriptor_error(
                    "descriptor.handle is null; set it to the Win32 NT handle the exporting "
                    "API returned.");

            if (descriptor.kind == ExternalHandleKind::OpaqueFd &&
                descriptor.handle > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
                throw descriptor_error(
                    "descriptor.handle is " + std::to_string(descriptor.handle) +
                    ", above the largest file descriptor, " +
                    std::to_string(std::numeric_limits<int>::max()) +
                    "; set it to the descriptor the exporting API returned.");

            if (descriptor.offset > descriptor.allocation_bytes ||
                bytes > descriptor.allocation_bytes - descriptor.offset)
                throw descriptor_error(
                    "the region of " + std::to_string(bytes) + " bytes at offset " +
                    std::to_string(descriptor.offset) + " ends past the allocation of " +
                    std::to_string(descriptor.allocation_bytes) +
                    " bytes that descriptor.allocation_bytes states.");
        }

        /**
         * @brief Throws unless device @p device_index is the one @p descriptor names.
         * @throws SushiException With InvalidAccess, naming both UUIDs when both exist.
         */
        void require_same_device(const InteropCapabilities& capabilities,
                                 const ExternalMemoryDescriptor& descriptor,
                                 std::size_t device_index)
        {
            if (!capabilities.device_uuid)
                throw descriptor_error(
                    "device " + std::to_string(device_index) + " reports no UUID, so it " +
                    "cannot be matched to descriptor.device; import on a device whose " +
                    "capabilities() hold a device_uuid.");

            if (*capabilities.device_uuid != descriptor.device)
                throw descriptor_error(
                    "descriptor.device is " + hex(descriptor.device) + " and device " +
                    std::to_string(device_index) + " is " + hex(*capabilities.device_uuid) +
                    "; import on the device that exported the handle.");
        }

        /// @brief Runs one step of releasing an import and logs a failure in place of a throw.
        template <typename Step>
        void run_release_step(const char* name, std::size_t device_index, Step&& step) noexcept
        {
            try
            {
                step();
            }
            catch (const std::exception& error)
            {
                SR_LOG_ERROR("Interop: {} threw '{}' while releasing an import on device {}; "
                             "the remaining release steps still run.",
                             name, error.what(), device_index);
            }
            catch (...)
            {
                SR_LOG_ERROR("Interop: {} threw an unknown exception while releasing an import "
                             "on device {}; the remaining release steps still run.",
                             name, device_index);
            }
        }

        /// @brief Names which steps of one import completed and what each produced.
        struct ImportProgress
        {
            Experimental::external_mem memory{}; /**< Holds the SYCL import handle. */
            void* address = nullptr;             /**< Holds the mapped address, once mapped. */
            bool imported = false;               /**< Tells whether memory is held. */
            bool mapped = false;                 /**< Tells whether address is mapped. */
            bool adopted = false;                /**< Tells whether address is registered. */
        };

        /// @brief Undoes the completed steps of @p progress in reverse order, each on its own.
        void release_import(Internal::Execution::RuntimeContext& context, std::size_t device_index,
                            const ImportProgress& progress) noexcept
        {
            if (progress.adopted)
                run_release_step("USMAllocator::forget", device_index, [&]
                {
                    context.memory().forget(progress.address);
                });
            if (progress.mapped)
                run_release_step("unmap_external_linear_memory", device_index, [&]
                {
                    Experimental::unmap_external_linear_memory(
                        progress.address, context.devices().queue(device_index));
                });
            if (progress.imported)
                run_release_step("release_external_memory", device_index, [&]
                {
                    Experimental::release_external_memory(
                        progress.memory, context.devices().queue(device_index));
                });
        }

        /// @brief Unmaps and releases imported device memory.
        class DeviceImportOwner final : public Internal::Memory::AllocationOwner
        {
            public:
                /// @brief Binds the owner to the importing context, queue index and imported handle.
                DeviceImportOwner(std::shared_ptr<Internal::Execution::RuntimeContext> context,
                                  std::size_t device_index, Experimental::external_mem memory) noexcept
                    : context_(std::move(context)), device_index_(device_index), memory_(memory)
                {
                }

                /// @brief Forgets, unmaps and releases @p address.
                void release(void* address) noexcept override
                {
                    ImportProgress progress;
                    progress.memory = memory_;
                    progress.address = address;
                    progress.imported = true;
                    progress.mapped = true;
                    progress.adopted = true;
                    release_import(*context_, device_index_, progress);
                }

            private:
                /// @brief Holds the importing context.
                std::shared_ptr<Internal::Execution::RuntimeContext> context_;
                /// @brief Holds the importing queue index.
                std::size_t device_index_;
                /// @brief Holds the imported memory handle.
                Experimental::external_mem memory_;
        };

        /// @brief Releases whichever import steps completed before an exception unwinds the caller.
        class ImportGuard
        {
            public:
                /// @brief Binds the guard to the importing context and queue index.
                ImportGuard(std::shared_ptr<Internal::Execution::RuntimeContext> context, std::size_t device_index) noexcept
                    : context_(std::move(context)), device_index_(device_index)
                {
                }

                /// @brief Releases the recorded steps in reverse order, unless disarmed.
                ~ImportGuard()
                {
                    if (armed_)
                        release_import(*context_, device_index_, progress_);
                }

                ImportGuard(const ImportGuard&) = delete;
                ImportGuard& operator=(const ImportGuard&) = delete;

                /// @brief Records the imported handle, arming the guard.
                void mark_imported(Experimental::external_mem memory) noexcept
                {
                    progress_.memory = memory;
                    progress_.imported = true;
                    armed_ = true;
                }

                /// @brief Records the mapped address.
                void mark_mapped(void* address) noexcept
                {
                    progress_.address = address;
                    progress_.mapped = true;
                }

                /// @brief Records that the address was adopted into the USM registry.
                void mark_adopted() noexcept { progress_.adopted = true; }

                /// @brief Disarms the guard once ownership has moved to its final owner.
                void disarm() noexcept { armed_ = false; }

            private:
                /// @brief Holds the importing context.
                std::shared_ptr<Internal::Execution::RuntimeContext> context_;
                /// @brief Holds the importing queue index.
                std::size_t device_index_;
                /// @brief Holds the steps completed so far.
                ImportProgress progress_;
                /// @brief Tells whether the destructor must release the completed steps.
                bool armed_ = false;
        };
#endif

    } // namespace

    namespace Detail
    {
        InteropCapabilities query_capabilities(Internal::Execution::RuntimeContext& context, std::size_t device_index)
        {
            sycl::queue& queue = context.devices().queue(device_index);
            return capabilities_for(context, queue, device_index);
        }

        Internal::Memory::Allocation export_host_memory(const std::shared_ptr<Internal::Execution::RuntimeContext>& context,
                                              std::size_t bytes, std::size_t device_index,
                                              std::size_t alignment)
        {
            alignment = std::max(alignment, alignof(std::max_align_t));
            SR_VERIFY(is_power_of_two(alignment),
                      ("export_host: alignment " + std::to_string(alignment) +
                       " is not a power of two.")
                          .c_str());

            if (bytes == 0)
                return Internal::Memory::Allocation();

            SR_VERIFY(bytes <= std::numeric_limits<std::size_t>::max() - (alignment - 1),
                      ("export_host: " + std::to_string(bytes) +
                       " bytes overflow std::size_t when rounded up to the alignment of " +
                       std::to_string(alignment) + ".")
                          .c_str());

            const std::size_t rounded = round_up(bytes, alignment);

            // The owner exists before the allocation it will free (modules/surface/interop/README.md).
            auto owner = std::make_unique<Internal::Memory::USMAllocationOwner>(context->shared_memory());
            void* address = context->memory().allocate(
                {Internal::Memory::USMKind::Host, rounded, alignment, device_index});
            if (address == nullptr)
                throw SushiException(Error{
                    ErrorCode::AllocationFailed,
                    "export_host: host allocation of " + std::to_string(rounded) +
                    " bytes, aligned to " + std::to_string(alignment) + ", failed on device " +
                    std::to_string(device_index) + "; capabilities(" +
                    std::to_string(device_index) + ").host_export tells whether its queue " +
                    "offers host USM."});

            return Internal::Memory::Allocation(address, rounded, std::move(owner));
        }

        Internal::Memory::Allocation import_device_memory(const std::shared_ptr<Internal::Execution::RuntimeContext>& context,
                                                const ExternalMemoryDescriptor& descriptor,
                                                std::size_t bytes, std::size_t device_index)
        {
#if defined(SYCL_EXT_ONEAPI_BINDLESS_IMAGES)
            sycl::queue& queue = context->devices().queue(device_index);
            const InteropCapabilities capabilities = capabilities_for(*context, queue, device_index);
            if (!capabilities.device_import)
                throw SushiException(Error{
                    ErrorCode::Unsupported,
                    "import_device: device " + std::to_string(device_index) +
                    " cannot import external memory; capabilities(" +
                    std::to_string(device_index) + ").device_import is false."});
            require_importable(descriptor, bytes);
            require_same_device(capabilities, descriptor, device_index);

            if (bytes == 0)
                return Internal::Memory::Allocation();

            ImportGuard guard(context, device_index);

            Experimental::external_mem memory{};
            if (K_PLATFORM_HANDLE_KIND == ExternalHandleKind::OpaqueWin32)
            {
                Experimental::external_mem_descriptor<Experimental::resource_win32_handle> native{
                    {reinterpret_cast<void*>(static_cast<std::uintptr_t>(descriptor.handle))},
                    Experimental::external_mem_handle_type::win32_nt_handle,
                    static_cast<std::size_t>(descriptor.allocation_bytes)};
                memory = Experimental::import_external_memory(native, queue);
            }
            else
            {
                Experimental::external_mem_descriptor<Experimental::resource_fd> native{
                    {static_cast<int>(descriptor.handle)},
                    Experimental::external_mem_handle_type::opaque_fd,
                    static_cast<std::size_t>(descriptor.allocation_bytes)};
                memory = Experimental::import_external_memory(native, queue);
            }
            guard.mark_imported(memory);

            void* address = Experimental::map_external_linear_memory(memory, descriptor.offset, bytes, queue);
            guard.mark_mapped(address);

            context->memory().adopt(address, device_index, bytes);
            guard.mark_adopted();

            auto owner = std::make_unique<DeviceImportOwner>(context, device_index, memory);
            guard.disarm();
            return Internal::Memory::Allocation(address, bytes, std::move(owner));
#else
            (void)context;
            (void)descriptor;
            (void)bytes;
            (void)device_index;
            throw SushiException(Error{
                ErrorCode::Unsupported,
                "import_device: this toolchain has no bindless images extension."});
#endif
        }

    } // namespace Detail
} // namespace SushiRuntime
