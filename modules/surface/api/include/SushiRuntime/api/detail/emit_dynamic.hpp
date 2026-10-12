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
 * @brief Declares the descriptions of the launches whose window is bound per step.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/api/data/state.hpp>
#include <SushiRuntime/api/detail/late_controls.hpp>
#include <SushiRuntime/api/detail/late_window.hpp>
#include <SushiRuntime/api/detail/node_description.hpp>
#include <SushiRuntime/api/vocabulary/dynamic.hpp>
#include <SushiRuntime/region/liveness.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/region/buffer_region.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Registers one four-byte slot as a read of the node that consumes it and
         *        hands over the observer of its buffer.
         * @param slot  A slot whose words are not null.
         * @param lives Gains the slot's observer when it watches an allocation.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        inline void register_device_slot(const DeviceSlot& slot,
                                         std::vector<BufferRegion>& reads,
                                         std::vector<LivenessObserver>& lives)
        {
            reads.push_back(BufferRegion{
                slot_buffer_key(slot),
                slot.index * sizeof(std::uint32_t),
                sizeof(std::uint32_t)});
            if (!slot.life.expired())
                lives.push_back(slot.life);
        }

        /**
         * @brief Creates the per-step controls of a late-bound node, pointed at the device
         *        count slot and the device base slot of a Dynamic.
         * @param reads Gains one four-byte region per slot the Dynamic has.
         * @param lives Gains the observer of each slot's buffer.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        [[nodiscard]] SUSHIRUNTIME_API std::shared_ptr<LateControls>
        make_late_bindings(const Dynamic& dyn, std::vector<BufferRegion>& reads,
                           std::vector<LivenessObserver>& lives);

        /**
         * @brief Describes a per-element launch whose window varies per step.
         *
         * The node is compiled once and follows per-step changes without a recompile; its
         * reads cover the full capacity and any device slot, so a slot's producer is always
         * a predecessor. See submit_late_window().
         */
        template <class F>
        NodeDescription dynamic_node(std::shared_ptr<LateControls> lb,
                                     std::uint32_t* fault,
                                     std::vector<BufferRegion> reads,
                                     std::vector<BufferRegion> writes,
                                     std::size_t capacity, F&& kernel, std::size_t width)
        {
            NodeDescription description;
            description.kind = NodeKind::Dynamic;
            description.reads = std::move(reads);
            description.writes = std::move(writes);
            description.width = width;
            description.work = CommandGroupWork(
                [kernel = std::forward<F>(kernel), capacity, lb, fault](sycl::handler& h)
                {
                    submit_late_window(h, lb.get(), capacity, fault, kernel);
                });
            description.bindings = std::move(lb);
            return description;
        }

        /**
         * @brief Describes a State rule with a late-bound window.
         * @param view       The State's own view; its keys are read at every plan build.
         * @param capacity   The State's size, which bounds the live window.
         * @param slot_reads The regions read besides the State's current field.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        template <typename T, class F>
        NodeDescription dynamic_state_node(std::shared_ptr<StateView<T>> view,
                                           std::size_t capacity,
                                           std::shared_ptr<LateControls> lb,
                                           std::uint32_t* fault,
                                           std::vector<BufferRegion> slot_reads,
                                           F&& kernel, LiveFields live = {})
        {
            NodeDescription description;
            description.kind = NodeKind::DynamicState;
            description.width = capacity;
            description.live = std::move(live);
            description.state_access =
                [view, slot_reads = std::move(slot_reads)](std::vector<BufferRegion>& reads,
                                                           std::vector<BufferRegion>& writes)
                {
                    reads.push_back(BufferRegion(static_cast<void*>(view->current)));
                    reads.insert(reads.end(), slot_reads.begin(), slot_reads.end());
                    writes.push_back(BufferRegion(static_cast<void*>(view->next)));
                };
            description.work = CommandGroupWork(
                [kernel = std::forward<F>(kernel), view, lb, capacity, fault](sycl::handler& h)
                {
                    const T* current = view->current;
                    T* next = view->next;
                    submit_late_window(h, lb.get(), capacity, fault,
                                       [kernel, current, next](std::size_t i)
                                       {
                                           kernel(i, current, next);
                                       });
                });
            description.bindings = std::move(lb);
            return description;
        }
    } // namespace Detail
} // namespace SushiRuntime
