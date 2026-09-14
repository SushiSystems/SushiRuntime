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
        namespace detail
        {
            /**
             * @brief Validates a buffer slot a late-bound control will read at dispatch.
             *
             * The rule is written in docs/ARCHITECTURE.md, "Late-bound device values".
             *
             * @param slot  Buffer holding the value; must be Residency::Host.
             * @param index Element index within @p slot.
             * @param what  Name of the calling factory, for the error message.
             * @return A pointer to the buffer's first element.
             * @throws Core::SushiException With invalid_access if @p slot is not
             *         Residency::Host or @p index is out of range.
             */
            inline const std::uint32_t* checked_device_slot(
                const Buffer<std::uint32_t>& slot, std::size_t index, const char* what)
            {
                if (slot.residency() != Residency::Host)
                    throw Core::SushiException(Core::Error{Core::Errc::invalid_access,
                        std::string(what) + ": the buffer is Residency::" +
                        residency_name(slot.residency()) +
                        ", but the value is read on the host while kernels may run; "
                        "allocate it with Residency::Host."});
                if (index >= slot.size())
                    throw Core::SushiException(Core::Error{Core::Errc::invalid_access,
                        std::string(what) + ": index " + std::to_string(index) +
                        " is out of range for a buffer of " +
                        std::to_string(slot.size()) + " elements."});
                return slot.data();
            }
        } // namespace detail

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

            /** @brief Enable predicate polled once per step; false skips the node. */
            using WhenProvider  = Core::Delegate<bool(), PROVIDER_CAPACITY>;

            /** @brief Size provider polled once per step; element count to iterate. */
            using CountProvider = Core::Delegate<std::size_t(), PROVIDER_CAPACITY>;

            /** @brief Base provider polled once per step; first element to iterate. */
            using BaseProvider  = Core::Delegate<std::size_t(), PROVIDER_CAPACITY>;

            WhenProvider  when_pred;  /**< Empty: the node is always enabled. */
            CountProvider count_fn;   /**< Empty: iterate the full baked range. */

            /**
             * @brief Residency::Host slot a predecessor writes the live count into.
             *
             * Set by sized_from_device(); null otherwise. When set it takes the place
             * of count_fn entirely, because the two answer different questions and
             * only one can win — see sized_from_device().
             */
            const std::uint32_t* device_count = nullptr;

            /** @brief Element index of the live count within device_count. */
            std::size_t device_count_index = 0;

            BaseProvider base_fn;     /**< Empty: iterate from element zero. */

            /**
             * @brief Residency::Host slot a predecessor writes the live base into.
             *
             * Set by based_at_device(); null otherwise. When set it replaces
             * base_fn, for the same reason sized_from_device() replaces count_fn.
             */
            const std::uint32_t* device_base = nullptr;

            /** @brief Element index of the live base within device_base. */
            std::size_t device_base_index = 0;

            // The chaining setters are ref-qualified so a move-only Dynamic chains in
            // both shapes: mutating a named Dynamic in place (the lvalue overload
            // returns a reference) and building one inline — when(p).and_sized(c) —
            // where the rvalue overload returns by value so the result moves straight
            // into add()'s by-value parameter.

            /**
             * @brief Sets the enable predicate on a named Dynamic.
             * @param pred Polled before each step; false skips the node that step.
             * @return *this, for chaining.
             */
            Dynamic& and_when(WhenProvider pred) &
            {
                when_pred = std::move(pred);
                return *this;
            }

            /**
             * @brief Sets the enable predicate while chaining inline.
             * @param pred Polled before each step; false skips the node that step.
             * @return This Dynamic, moved out by value into the next call.
             */
            Dynamic and_when(WhenProvider pred) &&
            {
                when_pred = std::move(pred);
                return std::move(*this);
            }

            /**
             * @brief Sets the live-size provider on a named Dynamic.
             * @param count Polled before each step; must not exceed the baked capacity.
             * @return *this, for chaining.
             */
            Dynamic& and_sized(CountProvider count) &
            {
                count_fn = std::move(count);
                return *this;
            }

            /**
             * @brief Sets the live-size provider while chaining inline.
             * @param count Polled before each step; must not exceed the baked capacity.
             * @return This Dynamic, moved out by value into the next call.
             */
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
             * @param counter Slot in a Residency::Host allocation a predecessor writes; checked at add().
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

            /**
             * @brief Points a Dynamic at a device-written count while chaining inline.
             *
             * Drops any host-side sized() provider, as the lvalue overload does.
             * @param counter Slot in a Residency::Host allocation a predecessor writes; checked at add().
             * @param index   Element index of the count within @p counter.
             * @return This Dynamic, moved out by value into the next call.
             */
            Dynamic and_sized_from_device(const std::uint32_t* counter, std::size_t index) &&
            {
                device_count = counter;
                device_count_index = index;
                count_fn = CountProvider{};
                return std::move(*this);
            }

            /**
             * @brief Sets the first-element provider on a named Dynamic.
             *
             * Clears any device-written base, since only one source can decide it.
             * @param base Polled before each step; the first element to iterate.
             * @return *this, for chaining.
             */
            Dynamic& and_based_at(BaseProvider base) &
            {
                base_fn = std::move(base);
                device_base = nullptr;
                return *this;
            }

            /**
             * @brief Sets the first-element provider while chaining inline.
             *
             * Clears any device-written base, as the lvalue overload does.
             * @param base Polled before each step; the first element to iterate.
             * @return This Dynamic, moved out by value into the next call.
             */
            Dynamic and_based_at(BaseProvider base) &&
            {
                base_fn = std::move(base);
                device_base = nullptr;
                return std::move(*this);
            }

            /**
             * @brief Points a named Dynamic at a device-written base; returns *this.
             *
             * Drops any host-side based_at() provider, since the two cannot both
             * decide where the node starts.
             * @param slot  Slot in a Residency::Host allocation a predecessor writes; checked at add().
             * @param index Element index of the base within @p slot.
             * @return *this, for chaining.
             */
            Dynamic& and_based_at_device(const std::uint32_t* slot, std::size_t index) &
            {
                device_base = slot;
                device_base_index = index;
                base_fn = BaseProvider{};
                return *this;
            }

            /**
             * @brief Points a Dynamic at a device-written base while chaining inline.
             *
             * Drops any host-side based_at() provider, as the lvalue overload does.
             * @param slot  Slot in a Residency::Host allocation a predecessor writes; checked at add().
             * @param index Element index of the base within @p slot.
             * @return This Dynamic, moved out by value into the next call.
             */
            Dynamic and_based_at_device(const std::uint32_t* slot, std::size_t index) &&
            {
                device_base = slot;
                device_base_index = index;
                base_fn = BaseProvider{};
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
         * Residency::Host, checked here; and the live count must not
         * exceed the capacity the node was compiled for, checked at dispatch, where
         * exceeding it fails the run rather than silently truncating the work.
         *
         * Replaces any sized() provider on the same Dynamic; when() still composes.
         *
         * @param counter Residency::Host buffer a predecessor writes the count into.
         * @param index   Element index of the count within @p counter.
         * @return A Dynamic carrying the device count source, ready to pass to add().
         * @throws Core::SushiException If @p counter is not Residency::Host or @p index
         *         is out of range.
         */
        inline Dynamic sized_from_device(const Buffer<std::uint32_t>& counter,
                                         std::size_t index = 0)
        {
            Dynamic d;
            d.device_count = detail::checked_device_slot(counter, index, "sized_from_device");
            d.device_count_index = index;
            return d;
        }

        /**
         * @brief Builds a Dynamic whose node starts at a per-step element offset.
         *
         * sized() says how many elements a node iterates; this says from where. The
         * pair is what a graph-coloured solver needs: each colour is a contiguous
         * slice of one constraint buffer, and every colour's offset shifts as soon
         * as constraints are added or removed. Without it the kernel has to reach
         * its own work through an index-indirection buffer, paying an extra load
         * per element for something the launch could have carried for free.
         *
         * The offset is applied by the runtime, so the callable still receives
         * absolute indices: `fn(base + i)` for i in [0, count). The window must fit
         * inside the capacity the node was compiled for; one that does not fails
         * the run rather than reading past the end.
         *
         * Composes with when() and sized()/sized_from_device().
         *
         * @param base Polled before each step; the first element to iterate.
         * @return A Dynamic carrying the base provider, ready to pass to add().
         */
        inline Dynamic based_at(Dynamic::BaseProvider base)
        {
            Dynamic d;
            d.base_fn = std::move(base);
            return d;
        }

        /**
         * @brief Builds a Dynamic whose base comes from a slot the device wrote.
         *
         * The device-sourced form of based_at(), and the companion of
         * sized_from_device(): a colouring pass that computes both a slice's offset
         * and its length on the device hands over both the same way, and the slot is
         * registered as one of the consuming task's reads so the producer is
         * necessarily ordered first.
         *
         * Replaces any based_at() provider on the same Dynamic; when() and the size
         * controls still compose.
         *
         * @param slot  Residency::Host buffer a predecessor writes the base into.
         * @param index Element index of the base within @p slot.
         * @return A Dynamic carrying the device base source, ready to pass to add().
         * @throws Core::SushiException If @p slot is not Residency::Host or @p index is
         *         out of range.
         */
        inline Dynamic based_at_device(const Buffer<std::uint32_t>& slot,
                                       std::size_t index = 0)
        {
            Dynamic d;
            d.device_base = detail::checked_device_slot(slot, index, "based_at_device");
            d.device_base_index = index;
            return d;
        }

    } // namespace API
} // namespace SushiRuntime
