/**************************************************************************/
/* advanced_controls.hpp                                                  */
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
 * @file advanced_controls.hpp
 * @brief Declares AdvancedControls, the read-only view of a runtime's devices and counters.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
 */

#pragma once

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/device/device_description.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/observability/transfer_counters.hpp>

#include <SushiRuntime/api/runtime/runtime_config.hpp>

namespace SushiRuntime
{
    class Runtime;

    /**
     * @brief Answers questions about a runtime's devices, workers and counters, and hands
     *        out the queue of a device.
     *
     * A view shares ownership of the runtime's context, so it answers after the Runtime
     * it came from is destroyed.
     * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
     */
    class AdvancedControls
    {
        public:
            /** @brief Returns how many devices the runtime drives; indices are [0, this). */
            [[nodiscard]] std::size_t device_count() const
            {
                return context_->devices().count();
            }

            /**
             * @brief Returns the device inventory in SYCL enumeration order.
             * @return Entry i describes DeviceIndex{i}; valid while a handle of this
             *         runtime lives.
             * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
             */
            [[nodiscard]] const std::vector<Internal::Device::DeviceDescription>& devices() const
            {
                return context_->devices().devices();
            }

            /**
             * @brief Returns the queue the runtime submits a device's work on.
             * @return A reference valid while a handle of this runtime lives.
             * @pre device names a device of this runtime.
             * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
             */
            [[nodiscard]] sycl::queue& queue(DeviceIndex device) const
            {
                return context_->devices().queue(device.value());
            }

            /** @brief Returns whether the queues were built to record device timing. */
            [[nodiscard]] bool is_profiling_enabled() const noexcept
            {
                return context_->settings().profiling;
            }

            /** @brief Returns whether the background thermal rebalancer is running. */
            [[nodiscard]] bool is_rebalancer_enabled() const
            {
                return context_->execution_engine().rebalancer_enabled();
            }

            /**
             * @brief Returns the worker pool as built, which may differ from the request.
             * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
             */
            [[nodiscard]] WorkerPool worker_pool() const
            {
                return context_->execution_engine().worker_pool();
            }

            /**
             * @brief Returns how many USM allocations of the runtime are live.
             * @return The allocations made through this runtime and not yet released: one per
             *         Buffer, two per State, and what its graphs and extensions hold.
             */
            [[nodiscard]] std::size_t live_allocation_count() const
            {
                return context_->memory().live_count();
            }

            /**
             * @brief Returns the bytes explicit transfers copied host to device since startup.
             * @return A cumulative count; implicit shared-USM migration is not in it.
             */
            [[nodiscard]] std::size_t host_to_device_bytes() const noexcept
            {
                return context_->transfers().host_to_device();
            }

            /**
             * @brief Returns the bytes explicit transfers copied device to host since startup.
             * @return A cumulative count; implicit shared-USM migration is not in it.
             */
            [[nodiscard]] std::size_t device_to_host_bytes() const noexcept
            {
                return context_->transfers().device_to_host();
            }

        private:
            friend class Runtime;

            /**
             * @brief Shares ownership of a runtime's context.
             * @param context Must not be null.
             */
            explicit AdvancedControls(
                std::shared_ptr<Internal::Execution::RuntimeContext> context) noexcept
                : context_(std::move(context))
            {
            }

            /// @brief Context the answers are read from; never null.
            std::shared_ptr<Internal::Execution::RuntimeContext> context_;
    };



} // namespace SushiRuntime
