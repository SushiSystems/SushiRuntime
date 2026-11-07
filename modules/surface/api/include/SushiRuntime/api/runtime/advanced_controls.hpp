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
#include <SushiRuntime/machine/device_description.hpp>

#include <SushiRuntime/api/runtime/runtime_config.hpp>
#include <SushiRuntime/context/context_handle.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/machine/worker_pool.hpp>

namespace SushiRuntime
{
    class Runtime;

    namespace Internal::Graph
    {
        /// @brief Names the port that may run graph nodes elsewhere, in task/offload_coordinator.hpp.
        class OffloadCoordinator;
    } // namespace Internal::Graph

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
            [[nodiscard]] SUSHIRUNTIME_API std::size_t device_count() const;

            /**
             * @brief Returns the device inventory in SYCL enumeration order.
             * @return Entry i describes DeviceIndex{i}; valid while a handle of this
             *         runtime lives.
             * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
             */
            [[nodiscard]] SUSHIRUNTIME_API const std::vector<DeviceDescription>& devices() const;

            /**
             * @brief Returns the queue the runtime submits a device's work on.
             * @return A reference valid while a handle of this runtime lives.
             * @pre device names a device of this runtime.
             * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
             */
            [[nodiscard]] SUSHIRUNTIME_API sycl::queue& queue(DeviceIndex device) const;

            /** @brief Returns whether the background thermal rebalancer is running. */
            [[nodiscard]] SUSHIRUNTIME_API bool is_rebalancer_enabled() const;

            /**
             * @brief Returns the worker pool as built, which may differ from the request.
             * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
             */
            [[nodiscard]] SUSHIRUNTIME_API WorkerPool worker_pool() const;

            /**
             * @brief Returns how many USM allocations of the runtime are live.
             * @return The allocations made through this runtime and not yet released: one per
             *         Buffer, two per State, and what its graphs and extensions hold.
             */
            [[nodiscard]] SUSHIRUNTIME_API std::size_t live_allocation_count() const;

            /**
             * @brief Returns the bytes explicit transfers copied host to device since startup.
             * @return A cumulative count; implicit shared-USM migration is not in it.
             */
            [[nodiscard]] SUSHIRUNTIME_API std::size_t host_to_device_bytes() const noexcept;

            /**
             * @brief Returns the bytes explicit transfers copied device to host since startup.
             * @return A cumulative count; implicit shared-USM migration is not in it.
             */
            [[nodiscard]] SUSHIRUNTIME_API std::size_t device_to_host_bytes() const noexcept;

            /**
             * @brief Installs the coordinator this runtime's runs offer their nodes to; null removes it.
             * @param coordinator Co-owned while installed; a runtime starts with none.
             * @pre No run of this runtime is in flight.
             * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
             */
            SUSHIRUNTIME_API void
            install_offload(std::shared_ptr<Internal::Graph::OffloadCoordinator> coordinator) const;

        private:
            friend class Runtime;

            /**
             * @brief Shares ownership of a runtime's context.
             * @param context Must not be empty.
             */
            explicit AdvancedControls(ContextHandle context) noexcept
                : context_(std::move(context))
            {
            }

            /** @brief The runtime the answers are read from; never empty. */
            ContextHandle context_;
    };



} // namespace SushiRuntime
