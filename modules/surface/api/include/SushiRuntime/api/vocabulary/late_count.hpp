/**************************************************************************/
/* late_count.hpp                                                         */
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
 * @file late_count.hpp
 * @brief Declares LateCount, a run count resolved from one live source.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/vocabulary/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>

#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/api/detail/handle_access.hpp>
#include <SushiRuntime/api/vocabulary/dynamic.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/memory/resource_liveness.hpp>
#include <SushiRuntime/memory/resource_region.hpp>

namespace SushiRuntime
{
    namespace API
    {
        namespace detail
        {
            struct LateCountAccess;
        } // namespace detail

        /**
         * @brief Holds a run count resolved once per step from exactly one source.
         * @see modules/surface/api/include/SushiRuntime/api/vocabulary/README.md
         */
        class LateCount final
        {
            public:
                /** @brief Host provider of a live count; the type Dynamic::sized() takes. */
                using Provider = Dynamic::CountProvider;

                /**
                 * @brief Builds a count that is the same on every step.
                 * @param count The live count and the plan size; must fit a uint32.
                 * @throws Core::SushiException With invalid_graph if @p count does not fit.
                 */
                [[nodiscard]] static LateCount fixed(std::size_t count)
                {
                    return LateCount(count, "LateCount::fixed");
                }

                /**
                 * @brief Builds a count a host provider returns, polled once per step.
                 * @param capacity Plan size; the provider must not return more. Must fit a uint32.
                 * @param provider Called once per step when the resolver node is submitted, on the
                 *        thread that dispatches the node; must not be empty.
                 * @throws Core::SushiException With invalid_access for an empty provider, or
                 *        invalid_graph if @p capacity does not fit.
                 */
                [[nodiscard]] static LateCount from_host(std::size_t capacity, Provider provider)
                {
                    LateCount count(capacity, "LateCount::from_host");
                    if (!provider)
                        throw Core::SushiException(Core::Error{Core::Errc::invalid_access,
                            "LateCount::from_host: the provider is empty; pass a callable that "
                            "returns the live count, or use LateCount::fixed for a constant."});
                    count.host_ = std::make_shared<const Provider>(std::move(provider));
                    return count;
                }

                /**
                 * @brief Builds a count a predecessor kernel writes, read on the device each step.
                 * @param capacity Plan size; the slot must not hold more. Must fit a uint32.
                 * @param slot     Residency::Host buffer; a graph that recorded the count refuses
                 *        to run with dangling_resource once the buffer is destroyed.
                 * @throws Core::SushiException With invalid_access if @p slot is not host-resident
                 *        or @p index is out of range, or invalid_graph if @p capacity does not fit.
                 */
                [[nodiscard]] static LateCount from_device(std::size_t capacity,
                                                           const Buffer<std::uint32_t>& slot,
                                                           std::size_t index = 0)
                {
                    LateCount count(capacity, "LateCount::from_device");
                    count.slot_ =
                        detail::checked_device_slot(slot, index, "LateCount::from_device");
                    return count;
                }

                /** @brief Returns the plan size, which the live count never exceeds. */
                [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

            private:
                friend struct detail::LateCountAccess;

                /**
                 * @brief Constructs a count with no live source, so its live value is the capacity.
                 * @param what The call the error message names.
                 * @throws Core::SushiException With invalid_graph if @p capacity exceeds a uint32.
                 */
                LateCount(std::size_t capacity, const char* what) : capacity_(capacity)
                {
                    constexpr std::size_t limit = std::numeric_limits<std::uint32_t>::max();
                    if (capacity > limit)
                        throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                            std::string(what) + ": capacity " + std::to_string(capacity) +
                            " exceeds " + std::to_string(limit) + ", the largest count a uint32 "
                            "live-count word holds; split the work into counts that fit."});
                }

                /** @brief Plan size and upper bound of the live count. */
                std::size_t capacity_;

                /** @brief Host provider shared by every copy; null unless built by from_host(). */
                std::shared_ptr<const Provider> host_;

                /** @brief Slot a kernel reads the count from; set only by from_device(). */
                detail::DeviceSlot slot_;
        };

        namespace detail
        {
            /**
             * @brief Reads the source of a LateCount for the recording and resolving code.
             * @see modules/surface/api/include/SushiRuntime/api/vocabulary/README.md
             */
            struct LateCountAccess
            {
                /** @brief Returns the host provider, or null when the count has none. */
                static const LateCount::Provider* host(const LateCount& count) noexcept
                {
                    return count.host_.get();
                }

                /** @brief Returns the device word, or null when the count has none. */
                static const std::uint32_t* slot(const LateCount& count) noexcept
                {
                    const DeviceSlot& slot = count.slot_;
                    return slot.words == nullptr ? nullptr : slot.words + slot.index;
                }

                /** @brief Returns the whole-buffer region a resolver of a device count reads. */
                static Memory::ResourceRegion slot_region(const LateCount& count) noexcept
                {
                    return Memory::ResourceRegion(slot_buffer_key(count.slot_));
                }

                /** @brief Returns the observer of the device slot's buffer. */
                static const Memory::LivenessObserver& slot_life(const LateCount& count) noexcept
                {
                    return count.slot_.life;
                }
            };
        } // namespace detail

    } // namespace API

    using API::LateCount;

} // namespace SushiRuntime
