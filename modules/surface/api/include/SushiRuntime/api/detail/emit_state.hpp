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

#include <sycl/sycl.hpp>

#include <SushiRuntime/api/data/state.hpp>
#include <SushiRuntime/api/detail/callable_traits.hpp>
#include <SushiRuntime/api/graph/backing_graph.hpp>
#include <SushiRuntime/api/launch/extent.hpp>

namespace SushiRuntime
{
    namespace API
    {
        namespace detail
        {
            /**
             * @brief Builds the replay of a rule that reads a State's current field into its next.
             * @param state The state the rule evolves; read at every replay.
             */
            template <typename T, class F>
            auto state_node(std::shared_ptr<StateView<T>> view, State<T>* state, F&& fn)
            {
                return [fn = std::forward<F>(fn), view, state]
                       (BackingGraph& bg, std::optional<std::uint32_t> registration)
                {
                    const std::size_t n = state->size();
                    bg.add_node([fn, view, n](sycl::handler& h)
                    {
                        // Read fresh each submit (each step) to follow the swap.
                        const T* cur = view->cur;
                        T* next = view->next;
                        h.parallel_for(sycl::range<1>(n),
                                       [=](sycl::id<1> i) { fn(i[0], cur, next); });
                    }, { state->read_key() }, { state->write_key() }, {}, registration);
                };
            }

            /** @brief Builds the replay of a State rule launched over an Extent. */
            template <typename T, int N, class F>
            auto state_extent_node(std::shared_ptr<StateView<T>> view, State<T>* state,
                                   Extent<N> e, F&& fn)
            {
                return [fn = std::forward<F>(fn), view, state, e]
                       (BackingGraph& bg, std::optional<std::uint32_t> registration)
                {
                    bg.add_node([fn, view, e](sycl::handler& h)
                    {
                        const T* cur = view->cur;
                        T* next = view->next;
                        const sycl::id<N> off = e.offset;
                        h.parallel_for(e.extent, [=](sycl::id<N> idx)
                        {
                            fn(idx + off, cur, next);
                        });
                    }, { state->read_key() }, { state->write_key() }, {}, registration);
                };
            }

            /**
             * @brief Builds the replay of a rule that writes one State from several current fields.
             * @tparam Views  A std::tuple of the read States' views, in argument order.
             * @tparam States A std::tuple of State pointers: the written one first, then the reads.
             */
            template <typename T, class Views, class States, class F>
            auto coupled_node(std::shared_ptr<StateView<T>> out_view, State<T>* out,
                              Views views, States states, F&& fn)
            {
                return [fn = std::forward<F>(fn), out_view, views, states, out]
                       (BackingGraph& bg, std::optional<std::uint32_t> registration)
                {
                    using Seq = std::make_index_sequence<std::tuple_size<Views>::value>;
                    const std::size_t n = out->size();
                    auto reads = read_keys_of(states,
                        std::make_index_sequence<std::tuple_size<States>::value>{});
                    bg.add_node([fn, out_view, views, n](sycl::handler& h)
                    {
                        const T* cur = out_view->cur;
                        T* next = out_view->next;
                        // Resolved host-side at submit, so the kernel captures raw pointers.
                        auto ins_ptrs = views_current(views, Seq{});
                        h.parallel_for(sycl::range<1>(n), [=](sycl::id<1> i)
                        {
                            invoke_state(fn, i[0], cur, next, ins_ptrs, Seq{});
                        });
                    }, reads, { out->write_key() }, {}, registration);
                };
            }
        } // namespace detail
    } // namespace API
} // namespace SushiRuntime
