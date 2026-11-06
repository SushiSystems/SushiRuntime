/**************************************************************************/
/* profiled_runtime.hpp                                                   */
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
 * @file profiled_runtime.hpp
 * @brief Declares ProfiledRuntime, what the profiler registration reads of one runtime, and the function that wraps a Runtime in it.
 * @author Mustafa Garip
 * @see modules/surface/profiling/README.md
 */

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <SushiRuntime/api/runtime/runtime.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/machine/device_description.hpp>
#include <SushiRuntime/report/run_observer.hpp>

namespace SushiRuntime
{
    /**
     * @brief Gives the profiler registration the facts and the run observations of one runtime.
     *
     * RuntimeProfilerParts reads a runtime through this interface only. profiled_runtime_of() wraps a
     * Runtime in it; InMemoryProfiledRuntime holds the same answers as plain values.
     * @see modules/surface/profiling/README.md
     */
    class ProfiledRuntime
    {
        public:
            /** @brief Answers whether the profiler's session wants its sources to measure now. */
            using WantedGate = bool (*)() noexcept;

            virtual ~ProfiledRuntime() = default;

            /** @brief Returns the name of the runtime's node; "node" when the machine states none. */
            [[nodiscard]] virtual std::string node_name() const = 0;

            /** @brief Returns the devices the runtime drives, indexed by their position. */
            [[nodiscard]] virtual std::vector<DeviceDescription> devices() const = 0;

            /** @brief Returns the driver version of device @p device; "unknown" when it does not answer. */
            [[nodiscard]] virtual std::string driver_of(std::size_t device) const = 0;

            /** @brief Returns how many workers the runtime runs. */
            [[nodiscard]] virtual std::size_t worker_count() const = 0;

            /** @brief Returns whether the runtime's rebalancer is on. */
            [[nodiscard]] virtual bool rebalancer_enabled() const = 0;

            /**
             * @brief Adds @p observer to the runtime's runs, behind @p wanted when it is not null.
             * @return The handle that removes the observer when it is reset or destroyed.
             * @pre observer is not null.
             */
            [[nodiscard]] virtual RunObserverHandle observe_runs(std::shared_ptr<RunObserver> observer,
                                                                 WantedGate wanted) = 0;
    };

    /**
     * @brief Wraps @p runtime in a ProfiledRuntime that shares ownership of the runtime's context.
     * @see modules/surface/profiling/README.md
     */
    [[nodiscard]] SUSHIRUNTIME_API std::shared_ptr<ProfiledRuntime> profiled_runtime_of(const Runtime& runtime);
} // namespace SushiRuntime
