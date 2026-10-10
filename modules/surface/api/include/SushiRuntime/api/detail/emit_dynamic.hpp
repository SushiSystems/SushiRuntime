/**************************************************************************/
/* emit_dynamic.hpp                                                       */
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
 * @file emit_dynamic.hpp
 * @brief Declares the replay callables of the launches whose window is bound per step.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/api/data/state.hpp>
#include <SushiRuntime/api/detail/handle_access.hpp>
#include <SushiRuntime/api/detail/late_window.hpp>
#include <SushiRuntime/api/graph/backing_graph.hpp>
#include <SushiRuntime/api/vocabulary/dynamic.hpp>
#include <SushiRuntime/memory/resource_region.hpp>
#include <SushiRuntime/task/late_bindings.hpp>

namespace SushiRuntime
{
    namespace API
    {
        namespace detail
        {
            /**
             * @brief Registers one four-byte slot as a read of the node that consumes it.
             * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
             */
            inline void register_device_slot(const std::uint32_t* slot, std::size_t index,
                                             std::vector<Core::ResourceRegion>& reads)
            {
                reads.push_back(Core::ResourceRegion{
                    const_cast<void*>(static_cast<const void*>(slot)),
                    index * sizeof(std::uint32_t),
                    sizeof(std::uint32_t)});
            }

            /**
             * @brief Copies a Dynamic's device count source onto the bindings and registers it
             *        as a read.
             * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
             */
            inline void adopt_device_bindings(::SushiRuntime::Graph::LateBindings& lb,
                                              const Dynamic& dyn,
                                              std::vector<Core::ResourceRegion>& reads)
            {
                const DeviceSlot count = DynamicAccess::count_slot(dyn);
                if (count.words != nullptr)
                {
                    lb.device_count = count.words;
                    lb.device_count_index = count.index;
                    register_device_slot(count.words, count.index, reads);
                }
                const DeviceSlot base = DynamicAccess::base_slot(dyn);
                if (base.words != nullptr)
                {
                    lb.device_base = base.words;
                    lb.device_base_index = base.index;
                    register_device_slot(base.words, base.index, reads);
                }
            }

            /**
             * @brief Builds the replay of a per-element launch whose window varies per step.
             *
             * The node is compiled once and follows per-step changes without a recompile; its
             * reads cover the full capacity and any device slot, so a slot's producer is always
             * a predecessor. See submit_late_window().
             */
            template <class F>
            auto dynamic_node(std::shared_ptr<::SushiRuntime::Graph::LateBindings> lb,
                              std::uint32_t* fault,
                              std::vector<Core::ResourceRegion> reads,
                              std::vector<Core::ResourceRegion> writes,
                              std::size_t capacity, F&& fn, std::size_t width)
            {
                return [fn = std::forward<F>(fn), capacity, lb, fault, reads = std::move(reads),
                        writes = std::move(writes), width]
                       (BackingGraph& bg, std::optional<std::uint32_t> registration)
                {
                    bg.add_node([fn, capacity, lb, fault](sycl::handler& h)
                    {
                        submit_late_window(h, *lb, capacity, fault, fn);
                    }, reads, writes, lb, {}, registration);
                    bg.width_last_node(width);
                };
            }

            /**
             * @brief Builds the replay of a State rule with a late-bound window.
             * @param state The state the rule evolves; its size is the capacity, read per replay.
             */
            template <typename T, class F>
            auto dynamic_state_node(std::shared_ptr<StateView<T>> view, State<T>* state,
                                    std::shared_ptr<::SushiRuntime::Graph::LateBindings> lb,
                                    std::uint32_t* fault,
                                    std::vector<Core::ResourceRegion> reads, F&& fn)
            {
                return [fn = std::forward<F>(fn), view, state, lb, fault, reads = std::move(reads)]
                       (BackingGraph& bg, std::optional<std::uint32_t> registration)
                {
                    const std::size_t capacity = state->size();
                    bg.add_node([fn, view, lb, capacity, fault](sycl::handler& h)
                    {
                        const T* cur = view->cur;
                        T* next = view->next;
                        submit_late_window(h, *lb, capacity, fault,
                                           [fn, cur, next](std::size_t i) { fn(i, cur, next); });
                    }, reads, { HandleAccess::write_key(*state) }, lb, {}, registration);
                    bg.width_last_node(capacity);
                };
            }
        } // namespace detail
    } // namespace API
} // namespace SushiRuntime
