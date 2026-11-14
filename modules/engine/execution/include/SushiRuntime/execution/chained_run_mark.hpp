/**************************************************************************/
/* chained_run_mark.hpp                                                   */
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
 * @file chained_run_mark.hpp
 * @brief Declares ChainedRunMark, which marks a run submitted outside the engine in progress.
 * @author Mustafa Garip
 * @see modules/engine/execution/README.md
 */

#pragma once

#include <SushiRuntime/execution/execution_engine.hpp>

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        /**
         * @brief Marks a chained or replayed run in progress on an engine until released or destroyed.
         * @see modules/engine/execution/README.md
         */
        class ChainedRunMark
        {
            public:
                /** @brief Marks a chained run in progress on @p engine. */
                explicit ChainedRunMark(ExecutionEngine& engine) noexcept : engine_(&engine)
                {
                    engine_->begin_chained_run();
                }

                /** @brief Clears the mark unless release() already did. */
                ~ChainedRunMark()
                {
                    release();
                }

                ChainedRunMark(const ChainedRunMark&) = delete;
                ChainedRunMark& operator=(const ChainedRunMark&) = delete;
                ChainedRunMark(ChainedRunMark&&) = delete;
                ChainedRunMark& operator=(ChainedRunMark&&) = delete;

                /** @brief Clears the mark; a second call does nothing. */
                void release() noexcept
                {
                    if (engine_ == nullptr)
                        return;
                    engine_->end_chained_run();
                    engine_ = nullptr;
                }

            private:
                /** @brief The marked engine; null once released. */
                ExecutionEngine* engine_;
        };

    } // namespace Execution
} // namespace SushiRuntime
