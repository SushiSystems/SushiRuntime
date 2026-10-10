/**************************************************************************/
/* emit_offload.hpp                                                       */
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
 * @file emit_offload.hpp
 * @brief Declares the replay callables of the tasks a remote worker may run.
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
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/memory/resource_region.hpp>
#include <SushiRuntime/task/task_types.hpp>

namespace SushiRuntime
{
    namespace API
    {
        namespace detail
        {
            /**
             * @brief Fills the metadata of an offloadable task: name, kernel and parameters.
             * @param name Persistent label; stored, not copied.
             */
            inline ::SushiRuntime::Graph::TaskMetadata offload_metadata(
                const char* name, ::SushiRuntime::Graph::OpID op,
                const std::vector<std::uint64_t>& params)
            {
                ::SushiRuntime::Graph::TaskMetadata meta{};
                meta.name = name;
                meta.op_id = op;
                for (std::size_t i = 0; i < params.size() &&
                     i < ::SushiRuntime::Graph::TaskMetadata::K_MAX_PARAMS; ++i)
                    meta.set_param<std::uint64_t>(static_cast<int>(i), params[i]);
                return meta;
            }

            /** @brief Builds the replay of an offloadable per-element task. */
            template <class F>
            auto offload_node(::SushiRuntime::Graph::OpID op, std::size_t n,
                              std::vector<Memory::ResourceRegion> reads,
                              std::vector<Memory::ResourceRegion> writes,
                              F&& local, std::vector<std::uint64_t> params)
            {
                return [fn = std::forward<F>(local), n, op, reads = std::move(reads),
                        writes = std::move(writes), params = std::move(params)]
                       (::SushiRuntime::Graph::TaskGraph& bg,
                        std::optional<std::uint32_t> registration)
                {
                    bg.add_task(offload_metadata("fluent_offload", op, params), reads, writes,
                        [fn, n](sycl::queue& q, const std::vector<sycl::event>&) -> sycl::event
                        {
                            return q.submit([&](sycl::handler& h)
                            {
                                h.parallel_for(sycl::range<1>(n),
                                               [fn](sycl::id<1> i) { fn(i[0]); });
                            });
                        }, {}, registration);
                };
            }

            /**
             * @brief Builds the replay of an offloadable State rule with a local fallback.
             * @param live_reads  One-element list re-pointed at the current field per step.
             * @param live_writes One-element list re-pointed at the next field per step.
             */
            template <typename T, class F>
            auto offload_state_node(::SushiRuntime::Graph::OpID op,
                                    std::shared_ptr<StateView<T>> view, State<T>* state,
                                    std::shared_ptr<std::vector<void*>> live_reads,
                                    std::shared_ptr<std::vector<void*>> live_writes,
                                    F&& local, std::vector<std::uint64_t> params)
            {
                return [fn = std::forward<F>(local), view, state, op, params = std::move(params),
                        live_reads, live_writes]
                       (::SushiRuntime::Graph::TaskGraph& bg,
                        std::optional<std::uint32_t> registration)
                {
                    const std::size_t n = state->size();
                    bg.add_offload_dynamic(offload_metadata("fluent_offload_state", op, params),
                        live_reads, live_writes,
                        [fn, view, n](sycl::queue& q,
                                      const std::vector<sycl::event>&) -> sycl::event
                        {
                            // Read at each submit so the fallback follows the swap.
                            const T* cur = view->cur;
                            T* next = view->next;
                            return q.submit([&](sycl::handler& h)
                            {
                                h.parallel_for(sycl::range<1>(n),
                                    [fn, cur, next](sycl::id<1> i)
                                    {
                                        fn(i[0], cur, next);
                                    });
                            });
                        }, {}, registration);
                };
            }
        } // namespace detail
    } // namespace API
} // namespace SushiRuntime
