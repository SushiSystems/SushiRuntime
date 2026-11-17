/**************************************************************************/
/* recorder.cpp                                                           */
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
 * @file recorder.cpp
 * @brief Implements the members of Recorder that reach the runtime's context.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
 */

#include <SushiRuntime/api/graph/recorder.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/api/api_log_category.hpp>
#include <SushiRuntime/api/detail/device_fault.hpp>
#include <SushiRuntime/api/detail/node_spec.hpp>
#include <SushiRuntime/api/detail/node_stamp.hpp>
#include <SushiRuntime/core/assert.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/error_translation.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/sycl/error_translation.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/context_access/context_access.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/task/handle_location.hpp>

#include "context_memory.hpp"
#include "host_slot_check.hpp"

namespace SushiRuntime
{
    namespace
    {
        /**
         * @brief Returns the residency SYCL reports for @p address on @p device, or none when that
         *        device's context does not know the address.
         */
        [[nodiscard]] std::optional<Residency> residency_of(
            Internal::Execution::RuntimeContext& context, const void* address, DeviceIndex device)
        {
            switch (Detail::usm_kind_of(context, address, device.value()))
            {
                case sycl::usm::alloc::host:
                    return Residency::Host;
                case sycl::usm::alloc::shared:
                    return Residency::Shared;
                case sycl::usm::alloc::device:
                    return Residency::Device;
                default:
                    return std::nullopt;
            }
        }
    } // namespace

    std::size_t Recorder::record(Detail::NodeDescription&& description,
                                 std::vector<LivenessObserver>& lives,
                                 const NodeOptions& options)
    {
        if (!admits(options))
            return rule_count();
        if (!admit(dead_region_refusal(description)))
            return rule_count();
        watch_handles(description);
        Detail::NodeSpec spec(std::move(description));
        spec.stamp = Detail::NodeStamp(options, label_of(options.name), label_of(options.origin));
        if (placement_scope_ && !options.placement_class.is_named())
        {
            spec.stamp.placement_class = placement_scope_->placement_class();
            spec.stamp.placement_key = placement_scope_->placement_key();
        }
        if (!scope_open_ || !scope_has_node_)
            rule_names_.push_back(nullptr);
        scope_has_node_ = true;
        spec.rule = static_cast<std::uint32_t>(rule_names_.size() - 1);

        specs_.push_back(std::move(spec));
        for (LivenessObserver& life : lives)
            watch(std::move(life));
        ++generation_;
        return rule_names_.size() - 1;
    }

    std::optional<Error> Recorder::dead_region_refusal(const Detail::NodeDescription& description) const
    {
        const Internal::Memory::HandlePorts ports =
            Internal::ContextAccess::context(context_).handle_ports();
        const auto refusal = [&](const std::vector<BufferRegion>& regions, const char* role) -> std::optional<Error>
        {
            for (std::size_t index = 0; index < regions.size(); ++index)
            {
                const GlobalHandle handle = regions[index].base;
                if (handle.is_null() || ports.residency_reader.inspect(handle).status == HandleStatus::Ok)
                    continue;
                return Error{ErrorCode::InvalidAccess,
                             "rule " + std::to_string(next_rule()) + ": " + role + " region " +
                             std::to_string(index) + " names handle slot " + std::to_string(handle.slot()) +
                             " generation " + std::to_string(handle.generation()) +
                             ", and this runtime holds no live handle for it: its buffer or borrow was "
                             "released, or another runtime made it. Name a buffer this runtime holds."};
            }
            return std::nullopt;
        };
        std::optional<Error> refused = refusal(description.reads, "read");
        if (!refused)
            refused = refusal(description.writes, "write");
        for (std::size_t index = 0; !refused && index < description.pointer_handles.size(); ++index)
        {
            const GlobalHandle handle = description.pointer_handles[index];
            if (!handle.is_null() && ports.residency_reader.inspect(handle).status != HandleStatus::Ok)
                refused = Error{ErrorCode::InvalidAccess,
                                "rule " + std::to_string(next_rule()) + ": pointer " + std::to_string(index) +
                                " names handle slot " + std::to_string(handle.slot()) + " generation " +
                                std::to_string(handle.generation()) +
                                ", and this runtime holds no live handle for it: its buffer or borrow was "
                                "released, or another runtime made it. Name a buffer this runtime holds."};
        }
        return refused;
    }

    void Recorder::watch_handles(const Detail::NodeDescription& description)
    {
        for (const GlobalHandle handle : description.pointer_handles)
        {
            if (handle.is_null())
                continue;
            const auto at = std::lower_bound(watched_handles_.begin(), watched_handles_.end(), handle);
            if (at == watched_handles_.end() || *at != handle)
                watched_handles_.insert(at, handle);
        }
    }

    bool Recorder::has_released_handle() const noexcept
    {
        if (watched_handles_.empty() || !context_)
            return false;
        const Internal::Memory::HandlePorts ports =
            Internal::ContextAccess::context(context_).handle_ports();
        for (const GlobalHandle handle : watched_handles_)
            if (ports.residency_reader.inspect(handle).status != HandleStatus::Ok)
                return true;
        return false;
    }

    bool Recorder::admit(std::optional<Error> refused)
    {
        if (!refused)
            return true;
        if (!refusal_)
        {
            SR_LOG_WARN(K_API_LOG_CATEGORY, "A recording call was refused with {}: {}; nothing was recorded, and the "
                        "next run of this graph returns this refusal.",
                        to_string(refused->code), refused->message);
            refusal_ = std::move(*refused);
        }
        else
        {
            SR_LOG_WARN(K_API_LOG_CATEGORY, "A later recording call was refused with {}: {}; nothing was recorded, "
                        "and only the first refusal of the graph is kept.",
                        to_string(refused->code), refused->message);
        }
        return false;
    }

    Result<void*> Recorder::scratch_block(std::size_t count, std::size_t element_size,
                                          DeviceIndex device, Residency residency)
    {
        const std::size_t bytes =
            Detail::checked_byte_count("Recorder::scratch", count, element_size);
        void* block = nullptr;
        std::optional<Error> refused;
        Result<void> allocated = Internal::SYCL::translate_exceptions(
            ErrorCode::AllocationFailed, ErrorCode::AllocationFailed, [&]()
        {
            std::shared_ptr<Internal::Execution::RuntimeContext> context =
                Internal::ContextAccess::shared(context_);
            void* raw = Detail::malloc_residency_on(*context, bytes, device, residency);
            if (raw == nullptr)
            {
                refused = Error{
                    ErrorCode::AllocationFailed,
                    "Recorder::scratch: the device returned no memory for " +
                    Detail::describe_request(count, element_size, device, residency) + "."};
                return;
            }
            // The block returns to its context when the recorder lets go of it.
            std::shared_ptr<void> owned(raw, [context, device](void* address)
            {
                context->memory().release(address, device.value());
            });
            scratch_.push_back(std::move(owned));
            block = raw;
        });
        if (!allocated)
            return make_error(std::move(allocated.error()));
        if (refused)
            return make_error(std::move(*refused));
        return block;
    }

    Result<BufferRegion> Recorder::borrowed_region(const void* address, std::size_t bytes,
                                                   DeviceIndex device)
    {
        Result<GlobalHandle> handle = Detail::borrow_handle(
            context_, const_cast<void*>(address), bytes, 1, 1, device,
            residency_of(Internal::ContextAccess::context(context_), address, device));
        if (!handle)
            return make_error(std::move(handle.error()));
        if (handle->is_null())
            return BufferRegion();

        // The borrow is dropped when the recorder forgets its nodes.
        Result<void> kept = Detail::translate_exceptions([&]()
        {
            const ContextHandle context = context_;
            const GlobalHandle borrowed = *handle;
            scratch_.push_back(std::shared_ptr<void>(static_cast<void*>(nullptr),
                [context, borrowed](void*) { Detail::release_handle(context, borrowed); }));
        });
        // A refused push has already dropped the borrow through the deleter.
        if (!kept)
            return make_error(std::move(kept.error()));
        return BufferRegion(*handle);
    }

    void Recorder::require_device_slots(const Dynamic& window) const
    {
        const std::optional<Error> refusal = Detail::host_device_slots_refusal(
            Internal::ContextAccess::context(context_), window, next_rule());
        SR_VERIFY(!refusal, refusal->message.c_str());
    }

    Result<std::uint32_t*> Recorder::fault_record(DeviceIndex device)
    {
        Result<std::uint32_t*> words =
            scratch<std::uint32_t>(Detail::K_DEVICE_FAULT_WORDS, device, Residency::Host);
        if (!words)
            return words;
        for (std::size_t w = 0; w < Detail::K_DEVICE_FAULT_WORDS; ++w)
            (*words)[w] = 0;
        steps_.register_fault_record(*words);
        return words;
    }

    Result<std::uint32_t*> Recorder::host_fault_record()
    {
        std::uint32_t* words = nullptr;
        Result<void> allocated = Detail::translate_exceptions(
            ErrorCode::AllocationFailed, [this, &words]()
        {
            std::shared_ptr<std::uint32_t[]> owned(
                new std::uint32_t[Detail::K_DEVICE_FAULT_WORDS]());
            scratch_.push_back(std::shared_ptr<void>(owned, owned.get()));
            steps_.register_fault_record(owned.get());
            words = owned.get();
        });
        if (!allocated)
            return make_error(std::move(allocated.error()));
        return words;
    }

    Result<std::uint32_t*> Recorder::fault_record_for(const Dynamic& window)
    {
        const Detail::DeviceSlot& count_slot = Detail::DynamicAccess::count_slot(window);
        const Detail::DeviceSlot& slot = (count_slot.words != nullptr)
            ? count_slot : Detail::DynamicAccess::base_slot(window);
        if (slot.words == nullptr)
            return host_fault_record();
        Internal::Execution::RuntimeContext& context = Internal::ContextAccess::context(context_);
        const std::optional<Internal::Graph::HandleLocation> location =
            Internal::Graph::locate(context.handle_ports().residency_reader, slot.buffer, context.devices().count());
        return fault_record(DeviceIndex{location ? location->device : 0});
    }
} // namespace SushiRuntime
