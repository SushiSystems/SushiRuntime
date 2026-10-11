/**************************************************************************/
/* emit_state.hpp                                                         */
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
 * @file emit_state.hpp
 * @brief Declares the descriptions of the update rules of a State.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <cstddef>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/api/data/state.hpp>
#include <SushiRuntime/api/detail/callable_traits.hpp>
#include <SushiRuntime/api/detail/emit_launch.hpp>
#include <SushiRuntime/api/detail/node_description.hpp>
#include <SushiRuntime/region/buffer_region.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Describes a State rule: read the current field, write the next.
         * @tparam Launch std::size_t, Extent<N> or NdRange<N>; the State's size for the
         *         whole field.
         * @param view The State's own view, read at every plan build and every submit.
         * @param live The live fields of a rule that names an operation.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        template <typename T, class Launch, class F>
        NodeDescription state_launch_node(std::shared_ptr<StateView<T>> view, Launch launch,
                                          F&& kernel, LiveFields live = {})
        {
            NodeDescription description;
            description.kind = NodeKind::State;
            description.width = launch_count(launch);
            description.live = std::move(live);
            description.state_access = [view](std::vector<BufferRegion>& reads,
                                              std::vector<BufferRegion>& writes)
            {
                reads.push_back(BufferRegion(static_cast<void*>(view->current)));
                writes.push_back(BufferRegion(static_cast<void*>(view->next)));
            };
            description.work = CommandGroupWork(
                [kernel = std::forward<F>(kernel), view, launch](sycl::handler& h)
                {
                    // The view is read at each submit, so the kernel follows the swap.
                    const T* current = view->current;
                    T* next = view->next;
                    submit_launch(h, launch, kernel, current, next);
                });
            return description;
        }

        /**
         * @brief Describes a rule that writes one State from several current fields.
         * @tparam Views  A std::tuple of the views of the States read, in argument order.
         * @tparam Launch std::size_t, Extent<N> or NdRange<N>; the written State's size for
         *         the whole field.
         * @param live The live fields of a rule that names an operation.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        template <typename T, class Views, class Launch, class F>
        NodeDescription coupled_launch_node(std::shared_ptr<StateView<T>> out_view, Views views,
                                            Launch launch, F&& kernel, LiveFields live = {})
        {
            using Seq = std::make_index_sequence<std::tuple_size<Views>::value>;
            NodeDescription description;
            description.kind = NodeKind::State;
            description.width = launch_count(launch);
            description.live = std::move(live);
            description.state_access = [out_view, views](std::vector<BufferRegion>& reads,
                                                         std::vector<BufferRegion>& writes)
            {
                reads = read_keys_of_views(views, Seq{});
                reads.insert(reads.begin(), BufferRegion(static_cast<void*>(out_view->current)));
                writes.push_back(BufferRegion(static_cast<void*>(out_view->next)));
            };
            description.work = CommandGroupWork(
                [kernel = std::forward<F>(kernel), out_view, views, launch](sycl::handler& h)
                {
                    const T* current = out_view->current;
                    T* next = out_view->next;
                    // The inputs are resolved at each submit, so the kernel holds raw pointers.
                    std::apply([&h, &kernel, &launch, current, next](auto... input)
                    {
                        submit_launch(h, launch, kernel, current, next, input...);
                    }, views_current(views, Seq{}));
                });
            return description;
        }
    } // namespace Detail
} // namespace SushiRuntime
