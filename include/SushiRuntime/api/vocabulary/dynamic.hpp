/**************************************************************************/
/* dynamic.hpp                                                            */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

#pragma once

#include <cstddef>
#include <functional>
#include <utility>

namespace SushiRuntime
{
    namespace API
    {
        /**
         * @brief Opt-in late-binding controls for a task, evaluated once per step.
         *
         * Passed as the leading argument to add() to make a node's iteration size
         * or its participation vary every step without recompiling the graph.
         * when() gates whether the node runs at all (a sleeping island, an
         * LOD-disabled system); sized() sets how many elements it iterates, which
         * must stay within the capacity its buffers were allocated for. The two
         * compose — when(pred).and_sized(count) — and each provider is polled on the
         * driver thread before the plan replays, so the compiled topology is reused
         * across steps and the graph compiles exactly once.
         */
        struct Dynamic
        {
            std::function<bool()>        when_pred;  /**< Empty: the node is always enabled. */
            std::function<std::size_t()> count_fn;   /**< Empty: iterate the full baked range. */

            /** @brief Adds (or replaces) the enable predicate; returns *this for chaining. */
            Dynamic& and_when(std::function<bool()> pred)
            {
                when_pred = std::move(pred);
                return *this;
            }

            /** @brief Adds (or replaces) the live-size provider; returns *this for chaining. */
            Dynamic& and_sized(std::function<std::size_t()> count)
            {
                count_fn = std::move(count);
                return *this;
            }
        };

        /**
         * @brief Builds a Dynamic that enables its node only while @p pred is true.
         * @param pred Polled before each step; false skips the node that step.
         * @return A Dynamic carrying the predicate, ready to pass to add().
         */
        inline Dynamic when(std::function<bool()> pred)
        {
            Dynamic d;
            d.when_pred = std::move(pred);
            return d;
        }

        /**
         * @brief Builds a Dynamic that sets its node's live iteration size per step.
         * @param count Polled before each step; must not exceed the baked capacity.
         * @return A Dynamic carrying the size provider, ready to pass to add().
         */
        inline Dynamic sized(std::function<std::size_t()> count)
        {
            Dynamic d;
            d.count_fn = std::move(count);
            return d;
        }

    } // namespace API
} // namespace SushiRuntime
