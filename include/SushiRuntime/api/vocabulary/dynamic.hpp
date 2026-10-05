/****************************************************************/
/* dynamic.hpp                                                  */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file dynamic.hpp
 * @brief Declares Dynamic, the late-binding controls of a task, and its factories.
 * @author Mustafa Garip
 * @see include/SushiRuntime/api/vocabulary/README.md
 */

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
             * @see docs/architecture/ARCHITECTURE.md, "Late-bound device values".
             * @param slot Buffer holding the value; must be Residency::Host.
             * @throws Core::SushiException With invalid_access if @p slot is not
             *        Residency::Host or @p index is out of range.
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
         * @brief Holds opt-in late-binding controls for a task, evaluated once per step.
         * @see include/SushiRuntime/api/vocabulary/README.md
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

            // Chaining setters are ref-qualified: lvalues return *this, rvalues return by value.

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
             * @brief Points a named Dynamic at a device-written count and drops any sized()
             *        provider.
             * @param counter Slot in a Residency::Host allocation a predecessor writes; checked at
             *        add().
             * @see include/SushiRuntime/api/vocabulary/README.md
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
             * @brief Points a named Dynamic at a device-written base and drops any based_at()
             *        provider.
             * @param slot Slot in a Residency::Host allocation a predecessor writes; checked at
             *        add().
             * @see include/SushiRuntime/api/vocabulary/README.md
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
         * @param counter Residency::Host buffer a predecessor writes the count into.
         * @throws Core::SushiException If @p counter is not Residency::Host or @p index
         *        is out of range.
         * @see include/SushiRuntime/api/vocabulary/README.md
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
         * @param base Polled before each step; the first element to iterate.
         * @see include/SushiRuntime/api/vocabulary/README.md
         */
        inline Dynamic based_at(Dynamic::BaseProvider base)
        {
            Dynamic d;
            d.base_fn = std::move(base);
            return d;
        }

        /**
         * @brief Builds a Dynamic whose base comes from a slot the device wrote.
         * @param slot Residency::Host buffer a predecessor writes the base into.
         * @throws Core::SushiException If @p slot is not Residency::Host or @p index is
         *        out of range.
         * @see include/SushiRuntime/api/vocabulary/README.md
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
