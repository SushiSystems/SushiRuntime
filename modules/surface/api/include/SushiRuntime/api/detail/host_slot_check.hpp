/**************************************************************************/
/* host_slot_check.hpp                                                    */
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
 * @file host_slot_check.hpp
 * @brief Declares the checks that a Dynamic's device slots lie in host USM.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <sycl/sycl.hpp>

#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/api/vocabulary/dynamic.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Reports which kind of USM an address is, in its owning context.
         * @param address The address to classify; resolved through the registry.
         * @return The SYCL allocation kind, or unknown when nothing resolves it.
         */
        inline sycl::usm::alloc usm_kind_of(Internal::Execution::RuntimeContext& context,
                                            void* address)
        {
            try
            {
                return sycl::get_pointer_type(
                    address, owning_queue(context, address).get_context());
            }
            catch (const sycl::exception&)
            {
                return sycl::usm::alloc::unknown;
            }
        }

        /**
         * @brief Returns the refusal of a slot that is neither null nor in host USM.
         * @param what The builder call that named the slot, which opens the message.
         * @return An InvalidAccess error for a slot that is not host USM and for one this
         *         context never allocated, or nothing.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        [[nodiscard]] inline std::optional<Error> host_slot_refusal(
            Internal::Execution::RuntimeContext& context, const std::uint32_t* slot,
            const char* what, std::size_t rule)
        {
            if (slot == nullptr)
                return std::nullopt;
            void* address = const_cast<void*>(static_cast<const void*>(slot));
            if (usm_kind_of(context, address) == sycl::usm::alloc::host)
                return std::nullopt;
            const std::string where = std::string(what) + ", rule " + std::to_string(rule);
            const std::optional<void*> base = context.memory().containing_base(address);
            if (!base)
                return Error{ErrorCode::InvalidAccess,
                             where + ": this runtime never allocated the slot, so its residency "
                             "is unknowable; pass a Residency::Host buffer this runtime "
                             "allocated, with the element's index."};
            if (usm_kind_of(context, *base) == sycl::usm::alloc::host)
                return std::nullopt;
            return Error{ErrorCode::InvalidAccess,
                         where + ": the slot lies inside an allocation that is not "
                         "Residency::Host, and the host reads it while kernels may run; pass a "
                         "Residency::Host buffer, with the element's index."};
        }

        /**
         * @brief Returns the refusal of a Dynamic whose device count or base slot is not host
         *        USM.
         *
         * Resolves each slot's owning queue through the context's registry and
         * requires sycl::usm::alloc::host there (docs/architecture/ARCHITECTURE.md,
         * "Late-bound device values"). A recorder asks it before it records the node.
         * @return The refusal of the count slot, else that of the base slot, else nothing.
         */
        [[nodiscard]] inline std::optional<Error> host_device_slots_refusal(
            Internal::Execution::RuntimeContext& context, const Dynamic& dyn, std::size_t rule)
        {
            std::optional<Error> refusal = host_slot_refusal(
                context, DynamicAccess::count_slot(dyn).words, "sized_from_device", rule);
            if (refusal)
                return refusal;
            return host_slot_refusal(context, DynamicAccess::base_slot(dyn).words,
                                     "based_at_device", rule);
        }
    } // namespace Detail
} // namespace SushiRuntime
