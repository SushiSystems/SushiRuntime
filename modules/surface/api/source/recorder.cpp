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
            std::shared_ptr<void> owned(raw, [context](void* address)
            {
                context->memory().release(address);
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
        const std::uint32_t* count_words = Detail::DynamicAccess::count_slot(window).words;
        const std::uint32_t* slot = (count_words != nullptr)
            ? count_words : Detail::DynamicAccess::base_slot(window).words;
        if (slot == nullptr)
            return host_fault_record();
        const void* address = slot;
        Internal::Memory::USMAllocator& memory =
            Internal::ContextAccess::context(context_).memory();
        const std::optional<const void*> base = memory.containing_base(address);
        const std::optional<std::size_t> device = memory.device_of(base ? *base : address);
        return fault_record(DeviceIndex{device.value_or(0)});
    }
} // namespace SushiRuntime
