/**************************************************************************/
/* submit_release.hpp                                                     */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file submit_release.hpp
 * @brief Declares the release of a kernel node's successors at its submit instead of at its completion.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#pragma once

#include <cstddef>

#include <sycl/sycl.hpp>

#include <SushiRuntime/scheduler/quiescence_latch.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/offload_state.hpp>
#include <SushiRuntime/task/task.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        /** @brief Dispatches one worker keeps in flight at most before it releases no successor at submit. */
        constexpr std::size_t K_SUBMIT_RELEASE_IN_FLIGHT = 16;

        /**
         * @brief Says whether @p successor may be released when @p submitted is submitted, to wait for
         *        its event on the device.
         * @return True when both are whole kernel nodes placed on one device and @p successor is neither
         *         the sentinel nor offloadable.
         */
        [[nodiscard]] inline bool may_release_at_submit(const Internal::Graph::Node& submitted,
                                                        const Internal::Graph::Node& successor) noexcept
        {
            // One command group per node, so the submitted event covers all of its device work.
            if (submitted.mode != Task::ExecutionMode::Kernel || successor.mode != Task::ExecutionMode::Kernel)
                return false;
            if (successor.is_sentinel || submitted.placed_split || successor.placed_split)
                return false;

            // One device, so the successor's queue shares the context of the event it waits for.
            if (!submitted.locality_checked_ || !successor.locality_checked_ ||
                successor.cached_queue_index_ != submitted.cached_queue_index_)
                return false;

            const Internal::Graph::OffloadState* offload = successor.offload_state();
            return offload == nullptr || !offload->offloadable;
        }

        /**
         * @brief Releases into @p ready each successor of @p submitted that may wait for @p event on the
         *        device and that waits for no other predecessor.
         * @pre The completion of @p submitted is not yet reported, and only the calling thread reports it.
         * @param in_flight Dispatches the calling worker has in flight; at K_SUBMIT_RELEASE_IN_FLIGHT
         *        nothing is released.
         * @return How many successors were released; @p latch counts each once.
         */
        template <class Queue>
        std::size_t release_successors_at_submit(Internal::Graph::Node& submitted, const sycl::event& event,
                                                 std::size_t in_flight, QuiescenceLatch& latch, Queue& ready)
        {
            if (in_flight >= K_SUBMIT_RELEASE_IN_FLIGHT)
                return 0;

            std::size_t released = 0;
            submitted.for_each_successor([&](Internal::Graph::Node* successor)
            {
                if (!may_release_at_submit(submitted, *successor) || !successor->release_at_submit(event))
                    return;
                successor->add_ref();
                latch.add();
                ready.push(successor);
                ++released;
            });
            return released;
        }

    } // namespace Internal::Scheduler
} // namespace SushiRuntime
