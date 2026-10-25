/**************************************************************************/
/* dispatch_recorder.cpp                                                  */
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
 * @file dispatch_recorder.cpp
 * @brief Implements DispatchRecorder: the borrowed buffers, the recorded task and the run.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#include "dispatch_recorder.hpp"

#include <cstddef>
#include <exception>
#include <string>
#include <utility>
#include <vector>

#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/api/graph/access.hpp>
#include <SushiRuntime/api/vocabulary/dependencies.hpp>
#include <SushiRuntime/api/vocabulary/placement.hpp>
#include <SushiRuntime/core/result.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        DispatchRecorder::DispatchRecorder(Graph graph, Interop interop)
            : graph_(std::move(graph)), interop_(std::move(interop)),
              pinned_life_(make_liveness())
        {

        }

        void DispatchRecorder::clear()
        {
            graph_.clear();
        }

        template <typename Role>
        Result<void> DispatchRecorder::add_views(const std::vector<BufferView>& views,
                                                 DeviceIndex device,
                                                 std::vector<BufferRegion>& regions,
                                                 std::vector<LivenessObserver>& lives) const
        {
            for (const BufferView& view : views)
            {
                // The worker cache holds recorded shared USM of the runtime's allocator.
                Result<Buffer<std::byte>> borrowed = interop_.borrow(
                    static_cast<std::byte*>(view.data), view.size_bytes, device,
                    Residency::Shared, pinned_life_);
                if (!borrowed)
                    return make_error(std::move(borrowed.error()));
                if (borrowed->empty())
                    continue;
                const Role role(*borrowed);
                regions.insert(regions.end(), role.keys.begin(), role.keys.end());
                lives.insert(lives.end(), role.lives.begin(), role.lives.end());
            }
            return {};
        }

        void DispatchRecorder::record(DispatchEntry& entry) noexcept
        {
            try
            {
                const DeviceIndex device{entry.job.dispatch.device_index};
                Access access;
                Result<void> borrowed =
                    add_views<Reads>(entry.inputs, device, access.reads, access.lives);
                if (borrowed)
                    borrowed = add_views<Writes>(entry.outputs, device, access.writes,
                                                 access.lives);
                if (!borrowed)
                {
                    entry.failure = "the task was not recorded: " + borrowed.error().message;
                    return;
                }

                // &entry lives in the batch's deque; clear() drops this task before the next batch.
                graph_.recorder().task(
                    std::move(access),
                    [&entry](sycl::queue& queue, const std::vector<sycl::event>& dependencies)
                    {
                        entry.execute(queue, dependencies);
                        return sycl::event{};
                    },
                    On(device));
            }
            catch (const std::exception& error)
            {
                entry.failure = std::string("the task was not recorded: ") + error.what();
            }
            catch (...)
            {
                entry.failure = "the task was not recorded: unknown exception";
            }
        }

        std::string DispatchRecorder::run()
        {
            try
            {
                if (graph_.node_count() == 0)
                    return std::string();
                Result<RunReport, Error> report = graph_.run();
                if (!report)
                    return report.error().message;
                return std::string();
            }
            catch (const std::exception& error)
            {
                return error.what();
            }
            catch (...)
            {
                return "unknown exception";
            }
        }

    } // namespace Distributed
} // namespace SushiRuntime
