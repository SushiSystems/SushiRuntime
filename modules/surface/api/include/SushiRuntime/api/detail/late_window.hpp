/**************************************************************************/
/* late_window.hpp                                                        */
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
 * @file late_window.hpp
 * @brief Declares the launch of a late-bound node over its live window.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include <sycl/sycl.hpp>

#include <SushiRuntime/api/detail/device_fault.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/export.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph
    {
        struct LateBindings;
    } // namespace Internal::Graph

    namespace Detail
    {
        /**
         * @brief Holds what a late-bound node's per-step controls say at one submit.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        struct LateWindow
        {
            std::size_t count = 0;                     /**< Host count; unused when rest_of_capacity. */
            std::size_t base = 0;                      /**< Host base. */
            const std::uint32_t* count_slot = nullptr; /**< Device-written count, or null. */
            const std::uint32_t* base_slot = nullptr;  /**< Device-written base, or null. */
            bool rest_of_capacity = false;             /**< No host count was set for the step. */
        };

        /**
         * @brief Reads a node's per-step controls once, for the submit of one step.
         * @return The host count and base, and the address of each device slot the node has.
         */
        [[nodiscard]] SUSHIRUNTIME_API LateWindow read_late_window(
            const Internal::Graph::LateBindings& bindings) noexcept;

        /**
         * @brief Holds the half-open element window a late-bound node iterates this step.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        struct LiveRange
        {
            std::size_t base = 0;
            std::size_t count = 0;
        };

        /**
         * @brief Resolves a host-sized late-bound node's base and count at dispatch; a
         *        window that states no host count takes the rest of capacity from its base.
         * @param fault The node's fault record; a window that does not fit is written there
         *        as K_DEVICE_FAULT_WINDOW.
         * @return The range, or nothing for a window that does not fit the capacity.
         */
        [[nodiscard]] inline std::optional<LiveRange> resolve_live_range(
            const LateWindow& window, std::size_t capacity, std::uint32_t* fault) noexcept
        {
            LiveRange range;
            range.base = window.base;

            // No host count means the rest of the capacity from the base, not the whole buffer.
            range.count = window.rest_of_capacity
                ? capacity - (range.base < capacity ? range.base : capacity)
                : window.count;

            if (range.base > capacity || range.count > capacity - range.base)
            {
                record_device_fault(fault, K_DEVICE_FAULT_WINDOW, range.base, range.count,
                                    capacity);
                return std::nullopt;
            }
            return range;
        }

        /**
         * @brief Submits a late-bound node's kernel over its live range, or an empty kernel
         *        for none.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        template <class F>
        inline void submit_live_range(sycl::handler& h, LiveRange range, const F& kernel)
        {
            if (range.count == 0)
            {
                h.single_task([]() {});
                return;
            }
            // The base offset is applied here, so kernels always receive absolute indices.
            const std::size_t base = range.base;
            h.parallel_for(sycl::range<1>(range.count),
                           [kernel, base](sycl::id<1> i) { kernel(base + i[0]); });
        }

        /**
         * @brief Carries a device-sized node's window sources into its kernel.
         */
        struct DeviceWindow
        {
            const std::uint32_t* count_slot = nullptr; /**< Device-written count, or null. */
            const std::uint32_t* base_slot = nullptr;  /**< Device-written base, or null. */
            std::size_t host_count = 0;      /**< Host count when @c count_slot is null. */
            std::size_t host_base = 0;       /**< Host base when @c base_slot is null. */
            std::size_t capacity = 0;        /**< Capacity the window must fit. */
            bool rest_of_capacity = false;   /**< Whether the step states no host count. */
            std::uint32_t* fault = nullptr;  /**< The node's fault record. */
        };

        /**
         * @brief Submits a late-bound node, reading a device count or base inside the kernel.
         *
         * A node with no device slot keeps its host-resolved range. One with a
         * slot runs at @p capacity, each work-item past the live count returns,
         * and a window that does not fit is recorded in @p fault for the step to
         * return (docs/design/GRAPH_REPLAY.md §D). A host-resolved window that does not
         * fit is recorded there too, and an empty kernel is submitted in its place.
         * @pre fault is not null.
         */
        template <class F>
        inline void submit_late_window(sycl::handler& h,
                                       const Internal::Graph::LateBindings& lb,
                                       std::size_t capacity, std::uint32_t* fault,
                                       const F& kernel)
        {
            const LateWindow window = read_late_window(lb);
            if (window.count_slot == nullptr && window.base_slot == nullptr)
            {
                const std::optional<LiveRange> range =
                    resolve_live_range(window, capacity, fault);
                submit_live_range(h, range ? *range : LiveRange{}, kernel);
                return;
            }

            DeviceWindow w;
            w.count_slot = window.count_slot;
            w.base_slot = window.base_slot;
            w.host_count = window.count;
            w.host_base = window.base;
            w.capacity = capacity;
            w.rest_of_capacity = window.rest_of_capacity;
            w.fault = fault;

            // One work-item even at capacity zero, so a fault there is still recorded.
            h.parallel_for(sycl::range<1>(capacity == 0 ? 1 : capacity),
                           [w, kernel](sycl::id<1> i)
            {
                const std::size_t base = (w.base_slot != nullptr)
                    ? static_cast<std::size_t>(*w.base_slot) : w.host_base;
                std::size_t count = w.host_count;
                if (w.count_slot != nullptr)
                    count = static_cast<std::size_t>(*w.count_slot);
                else if (w.rest_of_capacity)
                    count = w.capacity - (base < w.capacity ? base : w.capacity);
                if (base > w.capacity || count > w.capacity - base)
                {
                    if (i[0] == 0)
                        record_device_fault(w.fault, K_DEVICE_FAULT_WINDOW,
                                                    base, count, w.capacity);
                    return;
                }
                if (i[0] < count)
                    kernel(base + i[0]);
            });
        }
    } // namespace Detail
} // namespace SushiRuntime
