/**************************************************************************/
/* recording_rebalance_strategy.hpp                                       */
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
 * @file recording_rebalance_strategy.hpp
 * @brief Declares RecordingRebalanceStrategy, a RebalanceStrategy that counts its evaluations and migrates nothing.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#pragma once

#include <atomic>
#include <cstddef>

#include <SushiRuntime/scheduler/rebalancer_interfaces.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        /**
         * @brief Counts each evaluation and asks for no migration.
         *
         * The count is atomic, so a WorkloadCoordinator thread may evaluate while a test reads it.
         */
        class RecordingRebalanceStrategy final : public RebalanceStrategy
        {
            public:
                /**
                 * @brief Records one evaluation.
                 * @param telemetry Telemetry the caller supplies; it is not read.
                 * @param migrator  Migrator the caller supplies; it is not called.
                 */
                void evaluate_and_rebalance(const TelemetryProvider& telemetry, TaskMigrator& migrator) override
                {
                    static_cast<void>(telemetry);
                    static_cast<void>(migrator);
                    evaluations_.fetch_add(1, std::memory_order_relaxed);
                }

                /** @return How many evaluations ran. */
                size_t evaluations() const
                {
                    return evaluations_.load(std::memory_order_relaxed);
                }

            private:
                std::atomic<size_t> evaluations_{0};
        };
    } // namespace Internal::Scheduler
} // namespace SushiRuntime
