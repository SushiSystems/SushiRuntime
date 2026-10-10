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
#include <SushiRuntime/api/detail/handle_access.hpp>
#include <SushiRuntime/graph/task_graph.hpp>

namespace SushiRuntime
{
    namespace API
    {
        namespace detail
        {
            /**
             * @brief Adds the node of a rule that reads a State's current field into its next.
             * @tparam Launch std::size_t, Extent<N> or NdRange<N>.
             * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
             */
            template <typename T, class Launch, class F>
            void add_state_node(::SushiRuntime::Graph::TaskGraph& bg,
                                std::optional<std::uint32_t> registration,
                                const std::shared_ptr<StateView<T>>& view, const State<T>& state,
                                const Launch& launch, const F& fn)
            {
                bg.add_node([fn, view, launch](sycl::handler& h)
                {
                    // The view is read at each submit, so the kernel follows the swap.
                    const T* cur = view->cur;
                    T* next = view->next;
                    submit_launch(h, launch, fn, cur, next);
                }, { HandleAccess::read_key(state) }, { HandleAccess::write_key(state) }, {},
                registration);
            }

            /**
             * @brief Builds the replay of a State rule over the whole field.
             * @param state The state the rule evolves; its size is read at every replay.
             */
            template <typename T, class F>
            auto state_node(std::shared_ptr<StateView<T>> view, State<T>* state, F&& fn)
            {
                return [fn = std::forward<F>(fn), view, state]
                       (::SushiRuntime::Graph::TaskGraph& bg,
                        std::optional<std::uint32_t> registration)
                {
                    add_state_node(bg, registration, view, *state, state->size(), fn);
                };
            }

            /**
             * @brief Builds the replay of a State rule over a launch the caller gave.
             * @tparam Launch std::size_t, Extent<N> or NdRange<N>.
             */
            template <typename T, class Launch, class F>
            auto state_launch_node(std::shared_ptr<StateView<T>> view, State<T>* state,
                                   Launch launch, F&& fn)
            {
                return [fn = std::forward<F>(fn), view, state, launch]
                       (::SushiRuntime::Graph::TaskGraph& bg,
                        std::optional<std::uint32_t> registration)
                {
                    add_state_node(bg, registration, view, *state, launch, fn);
                };
            }

            /**
             * @brief Adds the node of a rule that writes one State from several current fields.
             * @tparam Views  A std::tuple of the read States' views, in argument order.
             * @tparam States A std::tuple of State pointers: the written one first, then the reads.
             * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
             */
            template <typename T, class Views, class States, class Launch, class F>
            void add_coupled_node(::SushiRuntime::Graph::TaskGraph& bg,
                                  std::optional<std::uint32_t> registration,
                                  const std::shared_ptr<StateView<T>>& out_view,
                                  const State<T>& out, const Views& views, const States& states,
                                  const Launch& launch, const F& fn)
            {
                using Seq = std::make_index_sequence<std::tuple_size<Views>::value>;
                auto reads = read_keys_of(states,
                    std::make_index_sequence<std::tuple_size<States>::value>{});
                bg.add_node([fn, out_view, views, launch](sycl::handler& h)
                {
                    const T* cur = out_view->cur;
                    T* next = out_view->next;
                    // The inputs are resolved at each submit, so the kernel holds raw pointers.
                    std::apply([&h, &fn, &launch, cur, next](auto... input)
                    {
                        submit_launch(h, launch, fn, cur, next, input...);
                    }, views_current(views, Seq{}));
                }, reads, { HandleAccess::write_key(out) }, {}, registration);
            }

            /**
             * @brief Builds the replay of a coupled rule over the whole field written.
             * @param out The State written; its size is read at every replay.
             */
            template <typename T, class Views, class States, class F>
            auto coupled_node(std::shared_ptr<StateView<T>> out_view, State<T>* out,
                              Views views, States states, F&& fn)
            {
                return [fn = std::forward<F>(fn), out_view, views, states, out]
                       (::SushiRuntime::Graph::TaskGraph& bg,
                        std::optional<std::uint32_t> registration)
                {
                    add_coupled_node(bg, registration, out_view, *out, views, states,
                                     out->size(), fn);
                };
            }

            /**
             * @brief Builds the replay of a coupled rule over a launch the caller gave.
             * @tparam Launch std::size_t, Extent<N> or NdRange<N>.
             */
            template <typename T, class Views, class States, class Launch, class F>
            auto coupled_launch_node(std::shared_ptr<StateView<T>> out_view, State<T>* out,
                                     Views views, States states, Launch launch, F&& fn)
            {
                return [fn = std::forward<F>(fn), out_view, views, states, out, launch]
                       (::SushiRuntime::Graph::TaskGraph& bg,
                        std::optional<std::uint32_t> registration)
                {
                    add_coupled_node(bg, registration, out_view, *out, views, states, launch, fn);
                };
            }
        } // namespace detail
    } // namespace API
} // namespace SushiRuntime
