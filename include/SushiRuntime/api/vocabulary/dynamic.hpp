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
#include <cstdint>
#include <string>
#include <utility>

#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/core/delegate.hpp>
#include <SushiRuntime/core/error.hpp>

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

            /**
             * @brief Host-readable slot a predecessor writes the live count into.
             *
             * Set by sized_from_device(); null otherwise. When set it takes the place
             * of count_fn entirely, because the two answer different questions and
             * only one can win — see sized_from_device().
             */
            const std::uint32_t* device_count = nullptr;

            /** @brief Element index of the live count within device_count. */
            std::size_t device_count_index = 0;

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

            /**
             * @brief Points a named Dynamic at a device-written count; returns *this.
             *
             * Drops any host-side sized() provider, since the two cannot both decide
             * the iteration size. See sized_from_device() for the contract.
             * @param counter Host-readable slot a predecessor writes.
             * @param index   Element index of the count within @p counter.
             * @return *this, for chaining.
             */
            Dynamic& and_sized_from_device(const std::uint32_t* counter, std::size_t index) &
            {
                device_count = counter;
                device_count_index = index;
                count_fn = CountProvider{};
                return *this;
            }

            /** @brief Points a Dynamic at a device-written count while chaining inline. */
            Dynamic and_sized_from_device(const std::uint32_t* counter, std::size_t index) &&
            {
                device_count = counter;
                device_count_index = index;
                count_fn = CountProvider{};
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

        /**
         * @brief Builds a Dynamic whose size comes from a count the device produced.
         *
         * sized() answers "how many elements does the host already know we want",
         * polled at the step boundary. This answers the question a producer/consumer
         * chain actually asks: how many did the *device* just produce. A broadphase
         * writes its pair count and the narrowphase must iterate exactly that many —
         * a number that does not exist yet when the step boundary runs, so a
         * host-side provider cannot supply it without a mid-step readback or a
         * second run.
         *
         * The count is instead read when the node is dispatched, which is after every
         * predecessor's event has completed, so a value a predecessor kernel wrote is
         * visible. The task that consumes the count is automatically ordered after
         * whoever writes @p counter, because add() registers @p counter as one of its
         * reads — the ordering is structural, not a note the caller must remember.
         *
         * Two constraints, both enforced rather than documented: @p counter must be
         * host-addressable (shared USM), checked here; and the live count must not
         * exceed the capacity the node was compiled for, checked at dispatch, where
         * exceeding it fails the run rather than silently truncating the work.
         *
         * Replaces any sized() provider on the same Dynamic; when() still composes.
         *
         * @param counter Host-readable buffer a predecessor writes the count into.
         * @param index   Element index of the count within @p counter.
         * @return A Dynamic carrying the device count source, ready to pass to add().
         * @throws Core::SushiException If @p counter is device-resident or empty.
         */
        inline Dynamic sized_from_device(const Buffer<std::uint32_t>& counter,
                                         std::size_t index = 0)
        {
            if (!counter.host_accessible())
                throw Core::SushiException(Core::Error{Core::Errc::invalid_access,
                    "sized_from_device: the counter buffer is device-resident, but the "
                    "iteration size is read on the host at dispatch; allocate it with "
                    "Residency::Shared."});
            if (index >= counter.size())
                throw Core::SushiException(Core::Error{Core::Errc::invalid_access,
                    "sized_from_device: index " + std::to_string(index) +
                    " is out of range for a counter buffer of " +
                    std::to_string(counter.size()) + " elements."});

            Dynamic d;
            d.device_count = counter.data();
            d.device_count_index = index;
            return d;
        }

    } // namespace API
} // namespace SushiRuntime
