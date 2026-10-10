/**************************************************************************/
/* dynamic.hpp                                                            */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
/**************************************************************************/

/**
 * @file dynamic.hpp
 * @brief Declares Dynamic, the late-binding controls of a task, and its factories.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/vocabulary/README.md
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
            /** @brief Names one word of a Residency::Host buffer a late-bound control reads. */
            struct DeviceSlot
            {
                const std::uint32_t* words = nullptr; /**< First element, or null for no slot. */
                std::size_t index = 0;                /**< Element that holds the value. */
            };

            /**
             * @brief Validates a buffer slot a late-bound control will read at dispatch.
             * @see docs/architecture/ARCHITECTURE.md, "Late-bound device values".
             * @param slot Buffer holding the value; must be Residency::Host.
             * @param what The call the error message names.
             * @throws Core::SushiException With invalid_access if @p slot is not
             *        Residency::Host or @p index is out of range.
             */
            inline DeviceSlot checked_device_slot(const Buffer<std::uint32_t>& slot,
                                                  std::size_t index, const char* what)
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
                        " is out of range for a buffer of " + std::to_string(slot.size()) +
                        " elements; pass an index below " + std::to_string(slot.size()) + "."});
                return DeviceSlot{slot.data(), index};
            }

            struct DynamicAccess;
        } // namespace detail

        /**
         * @brief Holds opt-in late-binding controls for a task, resolved once per step.
         * @see modules/surface/api/include/SushiRuntime/api/vocabulary/README.md
         */
        class Dynamic final
        {
            public:
                /** @brief Inline storage for each provider; a predicate captures little. */
                static constexpr std::size_t PROVIDER_CAPACITY = 48;

                /** @brief Enable predicate polled once per step; false skips the node. */
                using WhenProvider  = Core::Delegate<bool(), PROVIDER_CAPACITY>;

                /** @brief Size provider polled once per step; element count to iterate. */
                using CountProvider = Core::Delegate<std::size_t(), PROVIDER_CAPACITY>;

                /** @brief Base provider polled once per step; first element to iterate. */
                using BaseProvider  = Core::Delegate<std::size_t(), PROVIDER_CAPACITY>;

                /**
                 * @brief Sets the enable predicate on a named Dynamic.
                 * @param pred Polled before each step; false skips the node that step.
                 * @return *this, for chaining.
                 */
                Dynamic& when(WhenProvider pred) &
                {
                    when_ = std::move(pred);
                    return *this;
                }

                /**
                 * @brief Sets the enable predicate while chaining inline.
                 * @param pred Polled before each step; false skips the node that step.
                 * @return This Dynamic, moved out by value into the next call.
                 */
                Dynamic when(WhenProvider pred) &&
                {
                    return std::move(when(std::move(pred)));
                }

                /**
                 * @brief Sets the live-size provider on a named Dynamic and drops any device
                 *        count.
                 * @param count Polled before each step; must not exceed the baked capacity.
                 * @return *this, for chaining.
                 */
                Dynamic& sized(CountProvider count) &
                {
                    count_ = std::move(count);
                    count_slot_ = detail::DeviceSlot{};
                    return *this;
                }

                /**
                 * @brief Sets the live-size provider while chaining inline.
                 * @param count Polled before each step; must not exceed the baked capacity.
                 * @return This Dynamic, moved out by value into the next call.
                 */
                Dynamic sized(CountProvider count) &&
                {
                    return std::move(sized(std::move(count)));
                }

                /**
                 * @brief Points a named Dynamic at a device-written count and drops any sized()
                 *        provider.
                 * @param counter Residency::Host buffer a predecessor writes the count into.
                 * @throws Core::SushiException With invalid_access if @p counter is not
                 *        Residency::Host or @p index is out of range.
                 * @see modules/surface/api/include/SushiRuntime/api/vocabulary/README.md
                 */
                Dynamic& sized_from_device(const Buffer<std::uint32_t>& counter,
                                           std::size_t index = 0) &
                {
                    count_slot_ = detail::checked_device_slot(counter, index, "sized_from_device");
                    count_ = CountProvider{};
                    return *this;
                }

                /**
                 * @brief Points a Dynamic at a device-written count while chaining inline.
                 * @param counter Residency::Host buffer a predecessor writes the count into.
                 * @throws Core::SushiException As the lvalue overload.
                 * @return This Dynamic, moved out by value into the next call.
                 */
                Dynamic sized_from_device(const Buffer<std::uint32_t>& counter,
                                          std::size_t index = 0) &&
                {
                    return std::move(sized_from_device(counter, index));
                }

                /**
                 * @brief Sets the first-element provider on a named Dynamic and drops any device
                 *        base.
                 * @param base Polled before each step; the first element to iterate.
                 * @return *this, for chaining.
                 */
                Dynamic& based_at(BaseProvider base) &
                {
                    base_ = std::move(base);
                    base_slot_ = detail::DeviceSlot{};
                    return *this;
                }

                /**
                 * @brief Sets the first-element provider while chaining inline.
                 * @param base Polled before each step; the first element to iterate.
                 * @return This Dynamic, moved out by value into the next call.
                 */
                Dynamic based_at(BaseProvider base) &&
                {
                    return std::move(based_at(std::move(base)));
                }

                /**
                 * @brief Points a named Dynamic at a device-written base and drops any based_at()
                 *        provider.
                 * @param slot Residency::Host buffer a predecessor writes the base into.
                 * @throws Core::SushiException With invalid_access if @p slot is not
                 *        Residency::Host or @p index is out of range.
                 * @see modules/surface/api/include/SushiRuntime/api/vocabulary/README.md
                 */
                Dynamic& based_at_device(const Buffer<std::uint32_t>& slot,
                                         std::size_t index = 0) &
                {
                    base_slot_ = detail::checked_device_slot(slot, index, "based_at_device");
                    base_ = BaseProvider{};
                    return *this;
                }

                /**
                 * @brief Points a Dynamic at a device-written base while chaining inline.
                 * @param slot Residency::Host buffer a predecessor writes the base into.
                 * @throws Core::SushiException As the lvalue overload.
                 * @return This Dynamic, moved out by value into the next call.
                 */
                Dynamic based_at_device(const Buffer<std::uint32_t>& slot,
                                        std::size_t index = 0) &&
                {
                    return std::move(based_at_device(slot, index));
                }

                /** @brief Forwards to when() on a named Dynamic. */
                [[deprecated("use when")]] Dynamic& and_when(WhenProvider pred) &
                {
                    return when(std::move(pred));
                }

                /** @brief Forwards to when() while chaining inline. */
                [[deprecated("use when")]] Dynamic and_when(WhenProvider pred) &&
                {
                    return std::move(when(std::move(pred)));
                }

                /** @brief Forwards to sized() on a named Dynamic. */
                [[deprecated("use sized")]] Dynamic& and_sized(CountProvider count) &
                {
                    return sized(std::move(count));
                }

                /** @brief Forwards to sized() while chaining inline. */
                [[deprecated("use sized")]] Dynamic and_sized(CountProvider count) &&
                {
                    return std::move(sized(std::move(count)));
                }

                /** @brief Forwards to based_at() on a named Dynamic. */
                [[deprecated("use based_at")]] Dynamic& and_based_at(BaseProvider base) &
                {
                    return based_at(std::move(base));
                }

                /** @brief Forwards to based_at() while chaining inline. */
                [[deprecated("use based_at")]] Dynamic and_based_at(BaseProvider base) &&
                {
                    return std::move(based_at(std::move(base)));
                }

            private:
                friend struct detail::DynamicAccess;

                /** @brief Enable predicate; empty means the node is always enabled. */
                WhenProvider when_;

                /** @brief Host size provider; empty means the full baked range. */
                CountProvider count_;

                /** @brief Slot of a device-written live count; set only while count_ is empty. */
                detail::DeviceSlot count_slot_;

                /** @brief Host base provider; empty means element zero. */
                BaseProvider base_;

                /** @brief Slot of a device-written live base; set only while base_ is empty. */
                detail::DeviceSlot base_slot_;
        };

        namespace detail
        {
            /**
             * @brief Reads the controls of a Dynamic for the recording and stepping code.
             * @see modules/surface/api/include/SushiRuntime/api/vocabulary/README.md
             */
            struct DynamicAccess
            {
                /** @brief Returns the enable predicate; empty when none was set. */
                static const Dynamic::WhenProvider& when(const Dynamic& dynamic) noexcept
                {
                    return dynamic.when_;
                }

                /** @brief Returns the host size provider; empty when none was set. */
                static const Dynamic::CountProvider& count(const Dynamic& dynamic) noexcept
                {
                    return dynamic.count_;
                }

                /** @brief Returns the host base provider; empty when none was set. */
                static const Dynamic::BaseProvider& base(const Dynamic& dynamic) noexcept
                {
                    return dynamic.base_;
                }

                /** @brief Returns the device count slot; its words are null when none was set. */
                static DeviceSlot count_slot(const Dynamic& dynamic) noexcept
                {
                    return dynamic.count_slot_;
                }

                /** @brief Returns the device base slot; its words are null when none was set. */
                static DeviceSlot base_slot(const Dynamic& dynamic) noexcept
                {
                    return dynamic.base_slot_;
                }
            };
        } // namespace detail

        /**
         * @brief Builds a Dynamic that enables its node only while @p pred is true.
         * @param pred Polled before each step; false skips the node that step.
         * @return A Dynamic carrying the predicate, ready to pass to add().
         */
        [[nodiscard]] inline Dynamic when(Dynamic::WhenProvider pred)
        {
            return Dynamic().when(std::move(pred));
        }

        /**
         * @brief Builds a Dynamic that sets its node's live iteration size per step.
         * @param count Polled before each step; must not exceed the baked capacity.
         * @return A Dynamic carrying the size provider, ready to pass to add().
         */
        [[nodiscard]] inline Dynamic sized(Dynamic::CountProvider count)
        {
            return Dynamic().sized(std::move(count));
        }

        /**
         * @brief Builds a Dynamic whose size comes from a count the device produced.
         * @param counter Residency::Host buffer a predecessor writes the count into.
         * @throws Core::SushiException With invalid_access if @p counter is not
         *        Residency::Host or @p index is out of range.
         * @see modules/surface/api/include/SushiRuntime/api/vocabulary/README.md
         */
        [[nodiscard]] inline Dynamic sized_from_device(const Buffer<std::uint32_t>& counter,
                                                       std::size_t index = 0)
        {
            return Dynamic().sized_from_device(counter, index);
        }

        /**
         * @brief Builds a Dynamic whose node starts at a per-step element offset.
         * @param base Polled before each step; the first element to iterate.
         * @see modules/surface/api/include/SushiRuntime/api/vocabulary/README.md
         */
        [[nodiscard]] inline Dynamic based_at(Dynamic::BaseProvider base)
        {
            return Dynamic().based_at(std::move(base));
        }

        /**
         * @brief Builds a Dynamic whose base comes from a slot the device wrote.
         * @param slot Residency::Host buffer a predecessor writes the base into.
         * @throws Core::SushiException With invalid_access if @p slot is not
         *        Residency::Host or @p index is out of range.
         * @see modules/surface/api/include/SushiRuntime/api/vocabulary/README.md
         */
        [[nodiscard]] inline Dynamic based_at_device(const Buffer<std::uint32_t>& slot,
                                                     std::size_t index = 0)
        {
            return Dynamic().based_at_device(slot, index);
        }

    } // namespace API
} // namespace SushiRuntime
