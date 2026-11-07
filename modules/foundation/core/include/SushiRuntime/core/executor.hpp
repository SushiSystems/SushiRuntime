/**************************************************************************/
/* executor.hpp                                                           */
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
 * @file executor.hpp
 * @brief Declares Executor, the port a part that runs jobs takes them through.
 * @author Mustafa Garip
 * @see modules/foundation/core/README.md
 */

#pragma once

#include <functional>

namespace SushiRuntime
{
    /**
     * @brief Takes jobs and runs each one once.
     *
     * SeededExecutor runs a job only inside run_one(), on the calling thread, in an order a seed
     * fixes; ThreadPoolExecutor also runs jobs on threads it owns. Both pass ExecutorContract.
     * A job must not throw.
     * @see modules/foundation/core/README.md
     */
    class Executor
    {
        public:
            /** @brief A unit of work; it runs once. */
            using Job = std::function<void()>;

            /** @brief Drops the jobs still pending without running them. */
            virtual ~Executor() = default;

            /**
             * @brief Adds a job to the pending ones; any thread may call it, a job included.
             * @param job Not empty.
             */
            virtual void post(Job job) = 0;

            /**
             * @brief Runs one pending job on the calling thread.
             * @return False when no job was pending, true when one ran.
             */
            [[nodiscard]] virtual bool run_one() = 0;

        protected:
            Executor() = default;
            Executor(const Executor&) = default;
            Executor& operator=(const Executor&) = default;
            Executor(Executor&&) = default;
            Executor& operator=(Executor&&) = default;
    };
} // namespace SushiRuntime
