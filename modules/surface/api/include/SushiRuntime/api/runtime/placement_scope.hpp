/**************************************************************************/
/* placement_scope.hpp                                                    */
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
 * @file placement_scope.hpp
 * @brief Declares PlacementScope, the class and key that Recorder::within() stamps on nodes.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include <SushiRuntime/context/context_handle.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/placement_class.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/machine/node_facts.hpp>

#include <SushiRuntime/api/data/buffer.hpp>

namespace SushiRuntime
{
    /**
     * @brief Names a placement class and an optional key, and gives the device the rules choose.
     *
     * Runtime::placement_scope() builds one after it checks the class name. A copy shares the
     * runtime's context and machine facts, so a scope keeps both alive and follows the rules the
     * runtime holds when device() or buffer() is called, not the rules of the time it was made.
     * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
     */
    class PlacementScope
    {
        public:
            /**
             * @brief Holds a class and a key, with no runtime behind them.
             *
             * Such a scope has no rules to read: device() gives device 0 and buffer() refuses.
             * @param placement_class Class every node recorded in the scope takes.
             * @param placement_key   Key every such node takes; 0 is a key like any other.
             */
            PlacementScope(PlacementClass placement_class,
                           std::optional<std::uint64_t> placement_key) noexcept
                : placement_class_(placement_class), placement_key_(placement_key)
            {
            }

            /**
             * @brief Holds a class and a key, and the runtime whose rules resolve them.
             * @param context  The context whose rules and devices the scope reads.
             * @param topology The facts of the machine the context runs on; not null.
             */
            PlacementScope(PlacementClass placement_class,
                           std::optional<std::uint64_t> placement_key, ContextHandle context,
                           std::shared_ptr<const NodeFacts> topology) noexcept
                : placement_class_(placement_class),
                  placement_key_(placement_key),
                  context_(std::move(context)),
                  topology_(std::move(topology))
            {
            }

            /** @brief Returns the class the scope names. */
            [[nodiscard]] PlacementClass placement_class() const noexcept
            {
                return placement_class_;
            }

            /** @brief Returns the key the scope names, or none. */
            [[nodiscard]] std::optional<std::uint64_t> placement_key() const noexcept
            {
                return placement_key_;
            }

            /**
             * @brief Returns the local device the rules in force give this scope.
             * @return The device, or ErrorCode::InvalidArgument naming the rule and the device
             *         when the target is absent and the rule says Fail; Local and Wait give
             *         device 0, Wait after its bound has passed.
             * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
             */
            [[nodiscard]] SUSHIRUNTIME_API Result<DeviceIndex> device() const;

            /**
             * @brief Allocates a typed buffer of @p count elements on the scope's device.
             * @param alignment Byte alignment of the address; 0 asks for the device default.
             * @return The buffer, the error device() returns, an InvalidState error for a scope
             *         built without a runtime, or an AllocationFailed error.
             * @pre The byte size of count fits std::size_t, and alignment is 0 or a power of two.
             * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
             */
            template <typename T>
            [[nodiscard]] Result<Buffer<T>> buffer(std::size_t count,
                                                   Residency residency = Residency::Shared,
                                                   std::size_t alignment = 0) const
            {
                Result<DeviceIndex> placed = device();
                if (!placed)
                    return make_error(std::move(placed.error()));
                if (!context_)
                    return make_error(missing_runtime("PlacementScope::buffer"));
                return Buffer<T>::allocate("PlacementScope::buffer", context_, count, *placed,
                                           residency, alignment);
            }

        private:
            /**
             * @brief Returns the error of a call that needs the runtime a scope was built without.
             * @param call Names the call in the message.
             */
            [[nodiscard]] SUSHIRUNTIME_API static Error missing_runtime(const char* call);

            /** @brief The class the scope names. */
            PlacementClass placement_class_;

            /** @brief The key the scope names, or none. */
            std::optional<std::uint64_t> placement_key_;

            /** @brief The context whose rules the scope reads; empty without a runtime. */
            ContextHandle context_;

            /** @brief The facts of the machine; null without a runtime. */
            std::shared_ptr<const NodeFacts> topology_;
    };
} // namespace SushiRuntime
