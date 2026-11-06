/**************************************************************************/
/* run_observer_registry.hpp                                              */
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
 * @file run_observer_registry.hpp
 * @brief Declares RunObserverRegistry, the list of run observers a RuntimeContext keeps.
 * @author Mustafa Garip
 * @see modules/engine/execution/README.md
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/report/run_observer.hpp>
#include <SushiRuntime/execution/execution_log_category.hpp>

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        /** @brief Answers whether an observer wants device timing for a run armed now; must not throw. */
        using TimingGate = bool (*)() noexcept;

        /**
         * @brief Holds the observers added to a runtime, in the order they were added, each behind an optional gate.
         *
         * A reader takes one consistent list without a lock; the writers are serialised. RuntimeContext
         * owns one, and a test builds its own, so the gate rules need no device.
         * @see modules/engine/execution/README.md
         */
        class RunObserverRegistry final
        {
            public:
                RunObserverRegistry() = default;
                RunObserverRegistry(const RunObserverRegistry&) = delete;
                RunObserverRegistry& operator=(const RunObserverRegistry&) = delete;
                RunObserverRegistry(RunObserverRegistry&&) = delete;
                RunObserverRegistry& operator=(RunObserverRegistry&&) = delete;
                ~RunObserverRegistry() = default;

                /**
                 * @brief Adds an observer that receives every run delivered while its gate is open.
                 * @param observer Not null; the registry co-owns it until it is removed.
                 * @param gate Whether it wants timing now; null for always. A closed gate skips its deliveries.
                 * @return The id remove() takes.
                 */
                std::uint64_t add(std::shared_ptr<RunObserver> observer, TimingGate gate)
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    const std::shared_ptr<const Entries> current = std::atomic_load(&entries_);
                    auto next = std::make_shared<Entries>(*current);
                    const std::uint64_t id = next_id_++;
                    next->push_back(Entry{id, std::move(observer), gate});
                    std::atomic_store(&entries_, std::shared_ptr<const Entries>(std::move(next)));
                    return id;
                }

                /**
                 * @brief Removes the observer an add() call returned @p id for; an unknown id removes nothing.
                 *
                 * A failure to copy the list is logged at ERROR and leaves the observer added.
                 */
                void remove(std::uint64_t id) noexcept
                {
                    try
                    {
                        const std::lock_guard<std::mutex> lock(mutex_);
                        const std::shared_ptr<const Entries> current = std::atomic_load(&entries_);
                        auto next = std::make_shared<Entries>();
                        next->reserve(current->size());
                        for (const Entry& entry : *current)
                            if (entry.id != id)
                                next->push_back(entry);
                        std::atomic_store(&entries_, std::shared_ptr<const Entries>(std::move(next)));
                    }
                    catch (const std::exception& error)
                    {
                        SR_LOG_ERROR(K_EXECUTION_LOG_CATEGORY, "Removing run observer {} failed: {}; it stays added.",
                                     id, error.what());
                    }
                }

                /** @brief Removes every observer. */
                void clear() noexcept
                {
                    std::atomic_store(&entries_, std::make_shared<const Entries>());
                }

                /** @return Whether a run armed now is timed: an ungated observer is added or a gate is open; any thread. */
                [[nodiscard]] bool wants_device_timing() const noexcept
                {
                    const std::shared_ptr<const Entries> current = std::atomic_load(&entries_);
                    for (const Entry& entry : *current)
                        if (entry.gate == nullptr || entry.gate())
                            return true;
                    return false;
                }

                /** @brief Hands @p observation to every observer whose gate is open, in the order they were added; any thread. */
                void notify(const RunObservation& observation) const noexcept
                {
                    const std::shared_ptr<const Entries> current = std::atomic_load(&entries_);
                    for (const Entry& entry : *current)
                        if (entry.gate == nullptr || entry.gate())
                            entry.observer->on_run(observation);
                }

            private:
                /** @brief Names one added observer. */
                struct Entry
                {
                    std::uint64_t id = 0;                   /**< What add() returned. */
                    std::shared_ptr<RunObserver> observer;  /**< The observer; never null. */
                    TimingGate gate = nullptr;              /**< Whether it wants timing now; null for always. */
                };

                /** @brief The observers in the order they were added. */
                using Entries = std::vector<Entry>;

                /**
                 * @brief The added observers, copied whole on every add and remove; never null.
                 *
                 * Read and written only through the std::atomic_* functions for shared_ptr.
                 */
                std::shared_ptr<const Entries> entries_ = std::make_shared<const Entries>();

                /** @brief Serialises the writers of entries_. */
                std::mutex mutex_;

                /** @brief The id the next added observer takes. */
                std::uint64_t next_id_ = 1;
        };
    } // namespace Internal::Execution
} // namespace SushiRuntime
