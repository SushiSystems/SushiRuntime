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
 * @brief Declares the replay callables of the update rules of a State.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/api/data/state.hpp>
#include <SushiRuntime/api/detail/callable_traits.hpp>
#include <SushiRuntime/api/detail/emit_launch.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/memory/resource_region.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Adds the node of a rule that reads a State's current field into its next.
         * @tparam Launch std::size_t, Extent<N> or NdRange<N>.
         * @param view The State's own view; its fields are the node's read and write keys.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        template <typename T, class Launch, class F>
        void add_state_node(::SushiRuntime::Internal::Graph::TaskGraph& bg,
                            std::optional<std::uint32_t> registration,
                            const std::shared_ptr<StateView<T>>& view, const Launch& launch,
                            const F& fn)
        {
            bg.add_node([fn, view, launch](sycl::handler& h)
            {
                // The view is read at each submit, so the kernel follows the swap.
                const T* current = view->current;
                T* next = view->next;
                submit_launch(h, launch, fn, current, next);
            }, { Internal::Memory::ResourceRegion(static_cast<void*>(view->current)) },
            { Internal::Memory::ResourceRegion(static_cast<void*>(view->next)) }, {},
            registration).baked_width = launch_count(launch);
        }

        /**
         * @brief Builds the replay of a State rule.
         * @tparam Launch std::size_t, Extent<N> or NdRange<N>; the State's size for the
         *         whole field.
         * @param view The State's own view, read at every replay and every submit.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        template <typename T, class Launch, class F>
        auto state_launch_node(std::shared_ptr<StateView<T>> view, Launch launch, F&& fn)
        {
            return [fn = std::forward<F>(fn), view, launch]
                   (::SushiRuntime::Internal::Graph::TaskGraph& bg,
                    std::optional<std::uint32_t> registration)
            {
                add_state_node(bg, registration, view, launch, fn);
            };
        }

        /**
         * @brief Adds the node of a rule that writes one State from several current fields.
         * @tparam Views A std::tuple of the views of the States read, in argument order.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        template <typename T, class Views, class Launch, class F>
        void add_coupled_node(::SushiRuntime::Internal::Graph::TaskGraph& bg,
                              std::optional<std::uint32_t> registration,
                              const std::shared_ptr<StateView<T>>& out_view,
                              const Views& views, const Launch& launch, const F& fn)
        {
            using Seq = std::make_index_sequence<std::tuple_size<Views>::value>;
            std::vector<Internal::Memory::ResourceRegion> reads = read_keys_of_views(views, Seq{});
            reads.insert(reads.begin(),
                         Internal::Memory::ResourceRegion(static_cast<void*>(out_view->current)));
            bg.add_node([fn, out_view, views, launch](sycl::handler& h)
            {
                const T* current = out_view->current;
                T* next = out_view->next;
                // The inputs are resolved at each submit, so the kernel holds raw pointers.
                std::apply([&h, &fn, &launch, current, next](auto... input)
                {
                    submit_launch(h, launch, fn, current, next, input...);
                }, views_current(views, Seq{}));
            }, reads, { Internal::Memory::ResourceRegion(static_cast<void*>(out_view->next)) }, {},
            registration).baked_width = launch_count(launch);
        }

        /**
         * @brief Builds the replay of a coupled rule.
         * @tparam Views  A std::tuple of the views of the States read, in argument order.
         * @tparam Launch std::size_t, Extent<N> or NdRange<N>; the written State's size for
         *         the whole field.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        template <typename T, class Views, class Launch, class F>
        auto coupled_launch_node(std::shared_ptr<StateView<T>> out_view, Views views,
                                 Launch launch, F&& fn)
        {
            return [fn = std::forward<F>(fn), out_view, views, launch]
                   (::SushiRuntime::Internal::Graph::TaskGraph& bg,
                    std::optional<std::uint32_t> registration)
            {
                add_coupled_node(bg, registration, out_view, views, launch, fn);
            };
        }
    } // namespace Detail
} // namespace SushiRuntime
