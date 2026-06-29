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
#include <utility>

#include <SushiRuntime/core/delegate.hpp>

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
         *
         * The providers are stored in allocation-free Core::Delegate slots, so a
         * Dynamic is move-only (a stored provider has one owner). Build one from a
         * when()/sized() temporary and pass it straight to add(), or std::move a
         * named Dynamic into the call.
         */
        struct Dynamic
        {
            /** @brief Inline storage for each provider; a predicate captures little. */
            static constexpr std::size_t PROVIDER_CAPACITY = 48;

            using WhenProvider  = Core::Delegate<bool(), PROVIDER_CAPACITY>;
            using CountProvider = Core::Delegate<std::size_t(), PROVIDER_CAPACITY>;

            WhenProvider  when_pred;  /**< Empty: the node is always enabled. */
            CountProvider count_fn;   /**< Empty: iterate the full baked range. */

            // The chaining setters are ref-qualified so a move-only Dynamic chains in
            // both shapes: mutating a named Dynamic in place (the lvalue overload
            // returns a reference) and building one inline — when(p).and_sized(c) —
            // where the rvalue overload returns by value so the result moves straight
            // into add()'s by-value parameter.

            /** @brief Sets the enable predicate on a named Dynamic; returns *this. */
            Dynamic& and_when(WhenProvider pred) &
            {
                when_pred = std::move(pred);
                return *this;
            }

            /** @brief Sets the enable predicate while chaining inline; returns by value. */
            Dynamic and_when(WhenProvider pred) &&
            {
                when_pred = std::move(pred);
                return std::move(*this);
            }

            /** @brief Sets the live-size provider on a named Dynamic; returns *this. */
            Dynamic& and_sized(CountProvider count) &
            {
                count_fn = std::move(count);
                return *this;
            }

            /** @brief Sets the live-size provider while chaining inline; returns by value. */
            Dynamic and_sized(CountProvider count) &&
            {
                count_fn = std::move(count);
                return std::move(*this);
            }
        };

        /**
         * @brief Builds a Dynamic that enables its node only while @p pred is true.
         * @param pred Polled before each step; false skips the node that step.
         * @return A Dynamic carrying the predicate, ready to pass to add().
         */
        inline Dynamic when(Dynamic::WhenProvider pred)
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
        inline Dynamic sized(Dynamic::CountProvider count)
        {
            Dynamic d;
            d.count_fn = std::move(count);
            return d;
        }

    } // namespace API
} // namespace SushiRuntime
