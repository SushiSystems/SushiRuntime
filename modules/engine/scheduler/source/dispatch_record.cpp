/**************************************************************************/
/* dispatch_record.cpp                                                    */
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

/**
 * @file dispatch_record.cpp
 * @brief Implements the pairing-claim policy of a dispatch and the build of its polled event.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#include <utility>

#include <SushiRuntime/profile/profile.hpp>
#include <SushiRuntime/scheduler/dispatch_record.hpp>
#include <SushiRuntime/task/node_widths.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        /**
         * @brief Tells whether a dispatch may claim its device's clock pairing.
         */
        bool claims_pairing(bool profiling, bool host, const Internal::Graph::Node* node)
        {
            if (!profiling)
                return false;
            return !host || (node && !node->is_sentinel);
        }

        /**
         * @brief Tells whether a submission reached a device, which keeps a pairing claim.
         */
        bool submitted_device_work(bool host, const sycl::event& event,
                                   const std::vector<sycl::event>& handed_over)
        {
            if (!host || !handed_over.empty())
                return true;
            // A default event compares unequal to another under DPC++, so its complete status identifies it.
            return event.get_info<sycl::info::event::command_execution_status>()
                != sycl::info::event_command_status::complete;
        }

        /**
         * @brief Builds the ready event of a host node that ran inline.
         */
        EventPolling::PolledEvent completed_host_event(Task* task)
        {
            EventPolling::PolledEvent done;
            done.task = task;
            done.ready = true;
            return done;
        }

        /**
         * @brief Builds the event a worker pushes to EventPolling after a submission.
         */
        EventPolling::PolledEvent dispatched_event(const sycl::event& event, bool submitted, Task* task,
                                                   const Internal::Graph::Node* node, bool profiling,
                                                   std::uint32_t step, std::uint32_t device_index,
                                                   std::vector<sycl::event> handed_over)
        {
            const bool ready = !submitted;
            EventPolling::PolledEvent dispatched;
            dispatched.event = event;
            dispatched.task = task;
            dispatched.ready = ready;
            dispatched.has_event = !ready;
            dispatched.step = step;
            if (node && profiling)
                dispatched.widths = Internal::Graph::snapshot_widths(*node);
            dispatched.handed_over = std::move(handed_over);
            dispatched.device_index = device_index;
            if (profiling)
                dispatched.submit_thread = Internal::Profile::current_thread_index();
            return dispatched;
        }

    } // namespace Internal::Scheduler
} // namespace SushiRuntime
