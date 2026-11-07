/**************************************************************************/
/* executor_drive.hpp                                                     */
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
 * @file executor_drive.hpp
 * @brief Declares ExecutorDrive, the TransportDrive that posts pump rounds to an Executor.
 * @author Mustafa Garip
 * @see modules/extension/in_process_transport/README.md
 */

#pragma once

#include <cstdint>
#include <memory>

#include <SushiRuntime/core/clock.hpp>
#include <SushiRuntime/core/executor.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/in_process_transport/transport_drive.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Posts each round as a job of an executor and reads the clock it was given.
         *
         * Over a SeededExecutor and a ManualClock an endpoint starts no thread, so a test steps
         * the rounds and advances the time; post_round_at() leaves the round to the
         * InProcessFabric::wake_all() the test makes after it advances the clock.
         * @see modules/extension/in_process_transport/README.md
         */
        class SUSHIRUNTIME_API ExecutorDrive final : public TransportDrive
        {
            public:
                /**
                 * @brief Binds the drive to the executor its rounds run on and the clock it reads.
                 * @param executor Not null; shared with the caller, who steps it.
                 * @param clock Not null; shared with the caller, who advances it.
                 */
                ExecutorDrive(std::shared_ptr<Executor> executor, std::shared_ptr<const Clock> clock);

                ExecutorDrive(const ExecutorDrive&) = delete;
                ExecutorDrive& operator=(const ExecutorDrive&) = delete;
                ExecutorDrive(ExecutorDrive&&) = delete;
                ExecutorDrive& operator=(ExecutorDrive&&) = delete;

                /** @brief Stops the drive; a job it posted that runs later runs no round. */
                ~ExecutorDrive() override;

                /** @brief Binds the round; it starts nothing. */
                [[nodiscard]] Result<void> start(Round round) override;

                /** @brief Posts one job that runs the round, unless stop() came first. */
                void post_round() override;

                /** @brief Does nothing: the clock's owner wakes the endpoint after it advances it. */
                void post_round_at(std::uint64_t at_ns) override;

                /** @brief Makes every job posted and not yet run skip the round. */
                void stop() noexcept override;

                /** @brief Reads the clock the drive was given. */
                [[nodiscard]] std::uint64_t now() const noexcept override;

            private:
                struct Shared;

                /** @brief The executor the rounds run on. */
                std::shared_ptr<Executor> executor_;

                /** @brief The clock every reading uses. */
                std::shared_ptr<const Clock> clock_;

                /** @brief The round and the stop flag, shared with the posted jobs. */
                std::shared_ptr<Shared> shared_;
        };
    } // namespace Distributed
} // namespace SushiRuntime
