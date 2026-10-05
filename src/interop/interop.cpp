/****************************************************************/
/* interop.cpp                                                  */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file interop.cpp
 * @brief Defines the non-template entry points behind the Interop facade.
 * @author Sushi (mustafagarip@sushisystems.io)
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>

#include <sycl/sycl.hpp>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/interop/detail/bindless_detection.hpp>
#include <SushiRuntime/interop/interop.hpp>
#include <SushiRuntime/memory/usm_allocation_owner.hpp>

#if defined(SYCL_EXT_ONEAPI_BINDLESS_IMAGES)
#include <sycl/ext/oneapi/bindless_images.hpp>
#endif

namespace SushiRuntime
{
    namespace Interop
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
            InteropCapabilities capabilities_for(Execution::RuntimeContext& context, sycl::queue& queue,
                                                 std::size_t device_index)
            {
                sycl::device device = queue.get_device();

                InteropCapabilities capabilities;
                capabilities.host_export = true;
                capabilities.device_import = Detail::BINDLESS_IMAGES_AVAILABLE
                    && context.device_is_gpu(device_index)
                    && device.has(sycl::aspect::ext_oneapi_external_memory_import);
                if (device.has(sycl::aspect::ext_intel_device_info_uuid))
                {
                    DeviceUuid uuid;
                    uuid.bytes = device.get_info<sycl::ext::intel::info::device::uuid>();
                    capabilities.device_uuid = uuid;
                }
                return capabilities;
            }

#if defined(SYCL_EXT_ONEAPI_BINDLESS_IMAGES)
            /// @brief Unmaps and releases imported device memory.
            class DeviceImportOwner final : public Memory::IAllocationOwner
            {
                public:
                    /// @brief Binds the owner to the importing context, queue index and imported handle.
                    DeviceImportOwner(std::shared_ptr<Execution::RuntimeContext> context,
                                      std::size_t device_index, Experimental::external_mem memory) noexcept
                        : context_(std::move(context)), device_index_(device_index), memory_(memory)
                    {
                    }

                    /// @brief Forgets, unmaps and releases @p address.
                    void release(void* address) noexcept override
                    {
                        try
                        {
                            sycl::queue& queue = context_->get_queue(device_index_);
                            context_->forget_usm(address);
                            Experimental::unmap_external_linear_memory(address, queue);
                            Experimental::release_external_memory(memory_, queue);
                        }
                        catch (...)
                        {
                            // release never throws: a failed unwind here would terminate the process.
                        }
                    }

                private:
                    /// @brief Holds the importing context.
                    std::shared_ptr<Execution::RuntimeContext> context_;
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
                    ImportGuard(std::shared_ptr<Execution::RuntimeContext> context, std::size_t device_index) noexcept
                        : context_(std::move(context)), device_index_(device_index)
                    {
                    }

                    /// @brief Releases the recorded steps in reverse order, unless disarmed.
                    ~ImportGuard()
                    {
                        if (!armed_)
                            return;
                        try
                        {
                            sycl::queue& queue = context_->get_queue(device_index_);
                            if (adopted_)
                                context_->forget_usm(address_);
                            if (mapped_)
                                Experimental::unmap_external_linear_memory(address_, queue);
                            Experimental::release_external_memory(memory_, queue);
                        }
                        catch (...)
                        {
                            // A destructor running during unwind must never throw itself.
                        }
                    }

                    ImportGuard(const ImportGuard&) = delete;
                    ImportGuard& operator=(const ImportGuard&) = delete;

                    /// @brief Records the imported handle, arming the guard.
                    void mark_imported(Experimental::external_mem memory) noexcept
                    {
                        memory_ = memory;
                        armed_ = true;
                    }

                    /// @brief Records the mapped address.
                    void mark_mapped(void* address) noexcept
                    {
                        address_ = address;
                        mapped_ = true;
                    }

                    /// @brief Records that the address was adopted into the USM registry.
                    void mark_adopted() noexcept { adopted_ = true; }

                    /// @brief Disarms the guard once ownership has moved to its final owner.
                    void disarm() noexcept { armed_ = false; }

                private:
                    /// @brief Holds the importing context.
                    std::shared_ptr<Execution::RuntimeContext> context_;
                    /// @brief Holds the importing queue index.
                    std::size_t device_index_;
                    /// @brief Holds the imported memory handle, once armed.
                    Experimental::external_mem memory_{};
                    /// @brief Holds the mapped address, once mapped.
                    void* address_ = nullptr;
                    /// @brief Tells whether the address was mapped.
                    bool mapped_ = false;
                    /// @brief Tells whether the address was adopted into the USM registry.
                    bool adopted_ = false;
                    /// @brief Tells whether an import handle is held and must be released.
                    bool armed_ = false;
            };
#endif

        } // namespace

        namespace Detail
        {
            InteropCapabilities query_capabilities(Execution::RuntimeContext& context, std::size_t device_index)
            {
                sycl::queue& queue = context.get_queue(device_index);
                return capabilities_for(context, queue, device_index);
            }

            Memory::Allocation export_host_memory(const std::shared_ptr<Execution::RuntimeContext>& context,
                                                  std::size_t bytes, std::size_t device_index,
                                                  std::size_t alignment)
            {
                alignment = std::max(alignment, alignof(std::max_align_t));
                if (!is_power_of_two(alignment))
                    throw Core::SushiException(Core::Error{
                        Core::Errc::allocation_failed,
                        "Interop::export_host: alignment must be a power of two."});

                if (bytes == 0)
                    return Memory::Allocation();

                if (bytes > std::numeric_limits<std::size_t>::max() - (alignment - 1))
                    throw Core::SushiException(Core::Error{
                        Core::Errc::allocation_failed,
                        "Interop::export_host: requested size overflows when rounded to alignment."});

                const std::size_t rounded = round_up(bytes, alignment);

                // The owner exists before the allocation it will free (src/interop/README.md).
                auto owner = std::make_unique<Memory::UsmAllocationOwner>(context);
                void* address = context->aligned_malloc_host_on(alignment, rounded, device_index);
                if (address == nullptr)
                    throw Core::SushiException(Core::Error{
                        Core::Errc::allocation_failed,
                        "Interop::export_host: host allocation of " + std::to_string(rounded) +
                        " bytes failed."});

                return Memory::Allocation(address, rounded, std::move(owner));
            }

            Memory::Allocation import_device_memory(const std::shared_ptr<Execution::RuntimeContext>& context,
                                                    const ExternalMemoryDescriptor& descriptor,
                                                    std::size_t bytes, std::size_t device_index)
            {
#if defined(SYCL_EXT_ONEAPI_BINDLESS_IMAGES)
                sycl::queue& queue = context->get_queue(device_index);
                const InteropCapabilities capabilities = capabilities_for(*context, queue, device_index);
                if (!capabilities.device_import)
                    throw Core::SushiException(Core::Error{
                        Core::Errc::unsupported,
                        "Interop::import_device: the target device cannot import external memory."});
                if (!capabilities.device_uuid || *capabilities.device_uuid != descriptor.device)
                    throw Core::SushiException(Core::Error{
                        Core::Errc::invalid_access,
                        "Interop::import_device: the descriptor names a different device."});

                ImportGuard guard(context, device_index);

                Experimental::external_mem memory{};
                if (descriptor.kind == ExternalHandleKind::OpaqueWin32)
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

                context->adopt_usm(address, device_index, bytes);
                guard.mark_adopted();

                auto owner = std::make_unique<DeviceImportOwner>(context, device_index, memory);
                guard.disarm();
                return Memory::Allocation(address, bytes, std::move(owner));
#else
                (void)context;
                (void)descriptor;
                (void)bytes;
                (void)device_index;
                throw Core::SushiException(Core::Error{
                    Core::Errc::unsupported,
                    "Interop::import_device: this toolchain has no bindless images extension."});
#endif
            }

        } // namespace Detail
    } // namespace Interop
} // namespace SushiRuntime
