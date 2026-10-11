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
 * @file recorder.cpp
 * @brief Implements the members of Recorder that reach the runtime's context.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
 */

#include <SushiRuntime/api/graph/recorder.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/api/detail/device_fault.hpp>
#include <SushiRuntime/api/detail/error_translation.hpp>
#include <SushiRuntime/api/detail/node_spec.hpp>
#include <SushiRuntime/api/detail/node_stamp.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/execution/context_access.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>

#include "context_memory.hpp"
#include "host_slot_check.hpp"

namespace SushiRuntime
{
    std::size_t Recorder::record(Detail::NodeDescription&& description,
                                 std::vector<LivenessObserver>& lives,
                                 const NodeOptions& options)
    {
        if (!admits(options))
            return rule_count();
        Detail::NodeSpec spec(std::move(description));
        spec.stamp = Detail::NodeStamp(options, label_of(options.name));
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

    Result<void*> Recorder::scratch_block(std::size_t count, std::size_t element_size,
                                          DeviceIndex device, Residency residency)
    {
        const std::size_t bytes =
            Detail::checked_byte_count("Recorder::scratch", count, element_size);
        void* block = nullptr;
        Error refused;
        Error thrown = Detail::translate_exceptions(
            ErrorCode::AllocationFailed, ErrorCode::AllocationFailed, [&]()
        {
            std::shared_ptr<Internal::Execution::RuntimeContext> context =
                Detail::ContextAccess::shared(context_);
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
            std::shared_ptr<void> owned(raw, [context](void* address)
            {
                context->memory().release(address);
            });
            scratch_.push_back(std::move(owned));
            block = raw;
        });
        if (thrown.code != ErrorCode::Ok)
            return make_error(std::move(thrown));
        if (refused.code != ErrorCode::Ok)
            return make_error(std::move(refused));
        return block;
    }

    std::optional<Error> Recorder::device_slots_refusal(const Dynamic& window) const
    {
        return Detail::host_device_slots_refusal(Detail::ContextAccess::context(context_),
                                                 window, next_rule());
    }

    std::optional<Error> Recorder::placement_refusal(const char* call,
                                                     const On& placement) const
    {
        const std::size_t device = placement.device.value();
        const std::size_t count = Detail::ContextAccess::context(context_).devices().count();
        if (device < count)
            return std::nullopt;
        return Error{
            ErrorCode::InvalidAccess,
            std::string(call) + ", rule " + std::to_string(next_rule()) +
                ": On(DeviceIndex{" + std::to_string(device) +
                "}) names no device, the runtime has " + std::to_string(count) +
                " device(s); name a device below that, or place the kernel "
                "with On(buffer)."};
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
        Error thrown = Detail::translate_exceptions(
            ErrorCode::AllocationFailed, ErrorCode::AllocationFailed, [this, &words]()
        {
            std::shared_ptr<std::uint32_t[]> owned(
                new std::uint32_t[Detail::K_DEVICE_FAULT_WORDS]());
            scratch_.push_back(std::shared_ptr<void>(owned, owned.get()));
            steps_.register_fault_record(owned.get());
            words = owned.get();
        });
        if (thrown.code != ErrorCode::Ok)
            return make_error(std::move(thrown));
        return words;
    }

    Result<std::uint32_t*> Recorder::fault_record_for(const Dynamic& window)
    {
        const std::uint32_t* count_words = Detail::DynamicAccess::count_slot(window).words;
        const std::uint32_t* slot = (count_words != nullptr)
            ? count_words : Detail::DynamicAccess::base_slot(window).words;
        if (slot == nullptr)
            return host_fault_record();
        void* address = const_cast<void*>(static_cast<const void*>(slot));
        Internal::Memory::USMAllocator& memory =
            Detail::ContextAccess::context(context_).memory();
        const std::optional<void*> base = memory.containing_base(address);
        const std::optional<std::size_t> device = memory.device_of(base ? *base : address);
        return fault_record(DeviceIndex{device.value_or(0)});
    }
} // namespace SushiRuntime
