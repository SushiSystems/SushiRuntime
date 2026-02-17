/**************************************************************************/
/* event_polling.hpp                                                      */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/* All Rights Reserved.                                                   */
/*                                                                        */
/* CONFIDENTIAL: This software is the proprietary information of          */
/* Mustafa Garip & Sushi Systems. Unauthorized copying of this file,      */
/* via any medium is strictly prohibited.                                 */
/*                                                                        */
/* This source code and the intellectual property contained herein        */
/* is confidential and may not be disclosed, copied, or used without      */
/* explicit written permission from the copyright holders.                */
/**************************************************************************/

#pragma once

#include <atomic>
#include <vector>
#include <sycl/sycl.hpp>
#include <SushiRuntime/graph/node.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/logger.hpp>

namespace SushiRuntime
{
    namespace Scheduler
    {
        using namespace SushiRuntime::Core;

        /**
         * @brief Polls hardware events in a lock-free manner.
         */
        class EventPolling 
        {
            public:
                /** @brief Structure to link a SYCL event with a graph node. */
                struct PolledEvent 
                {
                    sycl::event event;
                    void* node_ptr; 
                };

            private:
                /** @brief Counter for adaptive backoff strategy. */
                size_t backoff_counter = 0;
                /** @brief Maximum value for the backoff counter. */
                static constexpr size_t MAX_BACKOFF = 16; 

            public:
                /**
                 * @brief Checks for completed events and triggers successors.
                 * 
                 * Uses adaptive backoff to reduce CPU and driver load.
                 * 
                 * // TODO: Replace active_events vector with a fixed-size buffer or span to reduce allocations.
                 * @param active_events List of events currently being monitored.
                 * @param local_queue Current thread's task queue.
                 */
                template<typename QueueType>
                void poll_events(std::vector<PolledEvent>& active_events, QueueType& local_queue) 
                {
                    if (active_events.empty()) return;

                    // skip checks if backoff is active to reduce overhead
                    if (backoff_counter > 0) 
                    {
                        backoff_counter--;
                        return;
                    }

                    bool progress = false;
                    for (auto it = active_events.begin(); it != active_events.end(); ) 
                    {
                        auto status = it->event.get_info<sycl::info::event::command_execution_status>();
                        if (status == sycl::info::event_command_status::complete) 
                        {
                            trigger_successors(it->node_ptr, local_queue);
                            it = active_events.erase(it); 
                            progress = true;
                        } 
                        else 
                        {
                            ++it;
                        }
                    }

                    // adjust backoff based on progress
                    if (progress) 
                    {
                        backoff_counter = 0;
                    }
                    else 
                    {
                        backoff_counter = std::min(backoff_counter + 2, MAX_BACKOFF);
                    }
                }

            private:
                /**
                 * @brief Activates next nodes in the graph when a task finishes.
                 * @param node_void Pointer to the finished node.
                 * @param queue Current thread's task queue to push new ready nodes.
                 */
                template<typename QueueType>
                void trigger_successors(void* node_void, QueueType& queue) 
                {
                    auto* node = static_cast<SushiRuntime::Graph::Node*>(node_void);
                    
                    // iterate through successors in a lock-free way
                    auto* curr = node->successor_list.load(std::memory_order_acquire);
                    while(curr) 
                    {
                        auto* succ = curr->target;
                        
                        // if all dependencies are met, push the node to the queue
                        if(succ->in_degree.fetch_sub(1, std::memory_order_acq_rel) == 1) 
                        {
                             queue.push(succ);
                        }
                        
                        curr = curr->next;
                    }
                }
        };
        
    } // namespace Scheduler
} // namespace SushiRuntime
