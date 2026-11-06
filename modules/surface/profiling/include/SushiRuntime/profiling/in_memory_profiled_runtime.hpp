/**************************************************************************/
/* in_memory_profiled_runtime.hpp                                         */
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
 * @file in_memory_profiled_runtime.hpp
 * @brief Declares InMemoryProfiledRuntime, a ProfiledRuntime that answers with the values it was given.
 * @author Mustafa Garip
 * @see modules/surface/profiling/README.md
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <SushiRuntime/profiling/profiled_runtime.hpp>

namespace SushiRuntime
{
    /**
     * @brief Answers every question about its runtime with a stored value and holds the observers added to it.
     *
     * It drives no device and starts no thread. A device is added with its driver; a device whose
     * driver was not set reports "unknown", as a device that does not answer does.
     * @see modules/surface/profiling/README.md
     */
    class InMemoryProfiledRuntime final : public ProfiledRuntime
    {
        public:
            /** @brief Builds a runtime named @p node with no device, no worker and the rebalancer off. */
            explicit InMemoryProfiledRuntime(std::string node = "node") : node_(std::move(node))
            {
            }

            /** @brief Adds @p device, which reports @p driver, after the devices already held. */
            void add_device(DeviceDescription device, std::string driver)
            {
                drivers_[device.index] = std::move(driver);
                devices_.push_back(std::move(device));
            }

            /** @brief Sets the worker count and the rebalancer state the runtime reports. */
            void set_workers(std::size_t count, bool rebalancer) noexcept
            {
                workers_ = count;
                rebalancer_ = rebalancer;
            }

            /** @brief Returns how many observers are added now. */
            [[nodiscard]] std::size_t observer_count() const noexcept
            {
                return observers_->size();
            }

            [[nodiscard]] std::string node_name() const override
            {
                return node_;
            }

            [[nodiscard]] std::vector<DeviceDescription> devices() const override
            {
                return devices_;
            }

            [[nodiscard]] std::string driver_of(std::size_t device) const override
            {
                const auto found = drivers_.find(device);
                return found != drivers_.end() ? found->second : std::string("unknown");
            }

            [[nodiscard]] std::size_t worker_count() const override
            {
                return workers_;
            }

            [[nodiscard]] bool rebalancer_enabled() const override
            {
                return rebalancer_;
            }

            /**
             * @brief Holds @p observer until the returned handle is reset or destroyed.
             * @param wanted Not applied; the observer is added whatever the gate says.
             */
            [[nodiscard]] RunObserverHandle observe_runs(std::shared_ptr<RunObserver> observer,
                                                         WantedGate wanted) override
            {
                static_cast<void>(wanted);
                const std::shared_ptr<Observers> observers = observers_;
                observers->push_back(observer);
                return RunObserverHandle([observers, observer]()
                {
                    observers->erase(std::remove(observers->begin(), observers->end(), observer), observers->end());
                });
            }

        private:
            /** @brief The observers added to the runtime. */
            using Observers = std::vector<std::shared_ptr<RunObserver>>;

            /** @brief The node's name. */
            std::string node_;

            /** @brief The devices, in the order added. */
            std::vector<DeviceDescription> devices_;

            /** @brief The driver of each device, by its index. */
            std::map<std::size_t, std::string> drivers_;

            /** @brief The worker count reported. */
            std::size_t workers_ = 0;

            /** @brief Whether the rebalancer is reported on. */
            bool rebalancer_ = false;

            /** @brief The observers added; shared so a handle that outlives the runtime removes safely. */
            std::shared_ptr<Observers> observers_ = std::make_shared<Observers>();
    };
} // namespace SushiRuntime
