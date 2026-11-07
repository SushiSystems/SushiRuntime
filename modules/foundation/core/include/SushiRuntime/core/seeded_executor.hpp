/**************************************************************************/
/* seeded_executor.hpp                                                    */
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
 * @file seeded_executor.hpp
 * @brief Declares SeededExecutor, which runs posted jobs one at a time in an order drawn from a seed.
 * @author Mustafa Garip
 * @see modules/foundation/core/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <SushiRuntime/core/executor.hpp>

namespace SushiRuntime
{
    /**
     * @brief Holds posted jobs and runs them on the calling thread, one per run_one(), in a
     *        seeded order.
     *
     * A SplitMix64 sequence the seed starts draws the next job, so one seed gives one order on
     * every platform. It starts no thread and is called from one thread.
     * @see modules/foundation/core/README.md
     */
    class SeededExecutor final : public Executor
    {
        public:
            /**
             * @brief Starts an executor whose order the seed fixes.
             * @param seed Any value; two executors of one seed draw the same order.
             */
            explicit SeededExecutor(std::uint64_t seed = 0) noexcept : seed_(seed), state_(seed)
            {
            }

            SeededExecutor(const SeededExecutor&) = delete;
            SeededExecutor& operator=(const SeededExecutor&) = delete;
            SeededExecutor(SeededExecutor&&) = delete;
            SeededExecutor& operator=(SeededExecutor&&) = delete;

            /** @brief Drops the jobs still pending without running them. */
            ~SeededExecutor() override = default;

            /**
             * @brief Adds a job to the pending ones.
             * @param job Not empty.
             */
            void post(Job job) override { pending_.push_back(std::move(job)); }

            /**
             * @brief Runs one pending job, chosen by the seeded sequence.
             * @return False when no job was pending, true when one ran.
             */
            [[nodiscard]] bool run_one() override
            {
                if (pending_.empty())
                    return false;
                const std::size_t chosen =
                    static_cast<std::size_t>(next_draw() % pending_.size());
                Job job = std::move(pending_[chosen]);
                pending_[chosen] = std::move(pending_.back());
                pending_.pop_back();
                job();
                return true;
            }

            /**
             * @brief Runs jobs until none is pending, the ones they post included.
             * @return The number of jobs that ran.
             */
            std::size_t run_all()
            {
                std::size_t ran = 0;
                while (run_one())
                    ++ran;
                return ran;
            }

            /** @brief Returns the number of jobs posted and not yet run. */
            [[nodiscard]] std::size_t pending() const noexcept { return pending_.size(); }

            /** @brief Returns the seed the executor was started with, to replay its order. */
            [[nodiscard]] std::uint64_t seed() const noexcept { return seed_; }

        private:
            /** @brief Returns the next value of the SplitMix64 sequence. */
            std::uint64_t next_draw() noexcept
            {
                state_ += 0x9E3779B97F4A7C15ull;
                std::uint64_t mixed = state_;
                mixed = (mixed ^ (mixed >> 30)) * 0xBF58476D1CE4E5B9ull;
                mixed = (mixed ^ (mixed >> 27)) * 0x94D049BB133111EBull;
                return mixed ^ (mixed >> 31);
            }

            /** @brief The seed the executor was started with. */
            std::uint64_t seed_;

            /** @brief The state of the SplitMix64 sequence. */
            std::uint64_t state_;

            /** @brief The jobs posted and not yet run, in no promised order. */
            std::vector<Job> pending_;
    };
} // namespace SushiRuntime
