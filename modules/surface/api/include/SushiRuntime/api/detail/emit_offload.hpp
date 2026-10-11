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
#include <string>
#include <utility>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/api/data/state.hpp>
#include <SushiRuntime/api/detail/emit_launch.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/memory/resource_region.hpp>
#include <SushiRuntime/task/task_types.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Refuses more parameters than the metadata of an offloadable task holds.
         * @param call  Names the call the user wrote in the message.
         * @param given How many parameters the call passed.
         * @throws SushiException With invalid_graph for more than
         *         TaskMetadata::K_MAX_PARAMS.
         */
        inline void require_offload_parameters(const char* call, std::size_t given)
        {
            const std::size_t limit = ::SushiRuntime::Internal::Graph::TaskMetadata::K_MAX_PARAMS;
            if (given <= limit)
                return;
            throw SushiException(Error{
                Errc::invalid_graph,
                std::string(call) + ": " + std::to_string(given) +
                    " parameters were given and an offloadable task carries at most " +
                    std::to_string(limit) + "; pass at most that many."});
        }

        /**
         * @brief Fills the metadata of an offloadable task: name, kernel and parameters.
         * @param name Persistent label; stored, not copied.
         * @pre params passed require_offload_parameters().
         */
        inline ::SushiRuntime::Internal::Graph::TaskMetadata offload_metadata(
            const char* name, ::SushiRuntime::OperationID op,
            const std::vector<std::uint64_t>& params)
        {
            ::SushiRuntime::Internal::Graph::TaskMetadata meta{};
            meta.name = name;
            meta.op_id = op;
            for (std::size_t i = 0; i < params.size(); ++i)
                meta.set_param<std::uint64_t>(static_cast<int>(i), params[i]);
            return meta;
        }

        /** @brief Builds the replay of an offloadable per-element task. */
        template <class F>
        auto offload_node(::SushiRuntime::OperationID op, std::size_t n,
                          std::vector<Internal::Memory::ResourceRegion> reads,
                          std::vector<Internal::Memory::ResourceRegion> writes,
                          F&& local, std::vector<std::uint64_t> params)
        {
            return [fn = std::forward<F>(local), n, op, reads = std::move(reads),
                    writes = std::move(writes), params = std::move(params)]
                   (::SushiRuntime::Internal::Graph::TaskGraph& bg,
                    std::optional<std::uint32_t> registration)
            {
                bg.add_task(offload_metadata("fluent_offload", op, params), reads, writes,
                    [fn, n](sycl::queue& q, const std::vector<sycl::event>&) -> sycl::event
                    {
                        return q.submit([&](sycl::handler& h)
                        {
                            submit_launch(h, n, fn);
                        });
                    }, {}, registration).baked_width = n;
            };
        }

        /**
         * @brief Builds the replay of an offloadable State rule with a local fallback.
         * @param view        The State's own view, read at every replay and every submit.
         * @param size        Elements in each field of the State.
         * @param live_reads  One-element list re-pointed at the current field per step.
         * @param live_writes One-element list re-pointed at the next field per step.
         */
        template <typename T, class F>
        auto offload_state_node(::SushiRuntime::OperationID op,
                                std::shared_ptr<StateView<T>> view, std::size_t size,
                                std::shared_ptr<std::vector<void*>> live_reads,
                                std::shared_ptr<std::vector<void*>> live_writes,
                                F&& local, std::vector<std::uint64_t> params)
        {
            return [fn = std::forward<F>(local), view, size, op, params = std::move(params),
                    live_reads, live_writes]
                   (::SushiRuntime::Internal::Graph::TaskGraph& bg,
                    std::optional<std::uint32_t> registration)
            {
                // The lists name the State's fields as they stand when the node is added.
                (*live_reads)[0] = view->current;
                (*live_writes)[0] = view->next;
                bg.add_offload_dynamic(offload_metadata("fluent_offload_state", op, params),
                    live_reads, live_writes,
                    [fn, view, size](sycl::queue& q,
                                     const std::vector<sycl::event>&) -> sycl::event
                    {
                        // Read at each submit so the fallback follows the swap.
                        const T* current = view->current;
                        T* next = view->next;
                        return q.submit([&](sycl::handler& h)
                        {
                            submit_launch(h, size, fn, current, next);
                        });
                    }, {}, registration).baked_width = size;
            };
        }
    } // namespace Detail
} // namespace SushiRuntime
