/**************************************************************************/
/* recorded_steps.hpp                                                     */
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
 * @file recorded_steps.hpp
 * @brief Declares RecordedSteps, the one recorded step a chainable plan replays.
 * @author Mustafa Garip
 * @see docs/design/GRAPH_REPLAY/README.md
 */

#pragma once

#include <sycl/sycl.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph
    {
        /**
         * @brief Holds one step of a plan recorded as a command graph, owned by the plan's ChainPlan.
         * @see docs/design/GRAPH_REPLAY/README.md
         */
        class RecordedSteps
        {
            public:
                /** @brief Frees the recorded graph and the host words it reads. */
                virtual ~RecordedSteps() = default;

                /**
                 * @brief Writes the late-bound controls of this step into the recorded words,
                 *        then submits the recorded step to @p queue without waiting.
                 * @pre No earlier replay that reads the words is still running.
                 * @return The event of the replayed step.
                 */
                [[nodiscard]] virtual sycl::event replay(sycl::queue& queue) = 0;

                /** @brief Returns whether a replay reads host words written before it. */
                [[nodiscard]] virtual bool reads_host_words() const noexcept = 0;

            protected:
                RecordedSteps() = default;
                RecordedSteps(const RecordedSteps&) = delete;
                RecordedSteps& operator=(const RecordedSteps&) = delete;
                RecordedSteps(RecordedSteps&&) = delete;
                RecordedSteps& operator=(RecordedSteps&&) = delete;
        };

    } // namespace Graph
} // namespace SushiRuntime
