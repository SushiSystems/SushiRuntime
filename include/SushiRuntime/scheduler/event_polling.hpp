/**************************************************************************/
/* event_polling.hpp                                                      */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

#pragma once

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>
#include <functional>
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
                /** @brief Structure to link a SYCL event with a graph node or task. */
                struct PolledEvent 
                {
                    sycl::event event;
                    SushiRuntime::Scheduler::Task* task = nullptr; 
                };

                struct ActiveEventList 
                {
                    static constexpr size_t MAX_EVENTS = 256;
                    PolledEvent events[MAX_EVENTS];
                    size_t count = 0;

                    bool is_empty() const { return count == 0; }
                    
                    bool push(const PolledEvent& pe) 
                    {
                        if (count < MAX_EVENTS) 
                        {
                            events[count++] = pe;
                            return true;
                        }
                        
                        return false;
                    }

                    void remove_at(size_t index) 
                    {
                        if (index < count) 
                        {
                            events[index] = events[count - 1];
                            count--;
                        }
                    }
                };

#ifdef SUSHI_DISTRIBUTED
                /**
                 * @brief Installs an observer fired for each completed task.
                 *
                 * Used in Master mode to bump the versions of any registered
                 * buffers the completed node wrote, keeping remote caches coherent.
                 */
                void set_completion_observer(std::function<void(SushiRuntime::Scheduler::Task*)> cb)
                {
                    on_node_complete_ = std::move(cb);
                }
#endif

            private:
#ifdef SUSHI_DISTRIBUTED
                /** @brief Optional per-completion observer (Master mode only). */
                std::function<void(SushiRuntime::Scheduler::Task*)> on_node_complete_;
#endif
                /** @brief Counter for adaptive backoff strategy. */
                size_t backoff_counter = 0;
                /** @brief Maximum value for the backoff counter. */
                static constexpr size_t MAX_BACKOFF = 64; 

            public:
                /**
                 * @brief Checks for completed events and triggers successors.
                 *
                 * Uses adaptive backoff to reduce CPU and driver load.
                 *
                 * @param active_events List of events currently being monitored.
                 * @param local_queue Current thread's task queue.
                 */
                template<typename QueueType>
                void poll_events(ActiveEventList& active_events, QueueType& local_queue) 
                {
                    if (active_events.is_empty()) return;

                    // skip checks if backoff is active to reduce overhead
                    if (backoff_counter > 0) 
                    {
                        if (backoff_counter > 16)
                            std::this_thread::yield();

                        backoff_counter--;
                        return;
                    }

                    bool progress = false;
                    for (size_t i = 0; i < active_events.count; ) 
                    {
                        auto status = active_events.events[i].event.get_info<sycl::info::event::command_execution_status>();
                        if (status == sycl::info::event_command_status::complete)
                        {
#ifdef SUSHI_DISTRIBUTED
                            // Observe completion before releasing successors, so a
                            // write's version bump is visible to any dependent task
                            // (e.g. an offloaded reader) by the time it is dispatched.
                            if (on_node_complete_)
                                on_node_complete_(active_events.events[i].task);
#endif
                            trigger_successors(active_events.events[i].task, local_queue);

                            if (active_events.events[i].task)
                                active_events.events[i].task->release();

                            active_events.remove_at(i); // O(1) Silme
                            progress = true;
                            // i'yi artırma, çünkü son eleman bu index'e taşındı
                        } 
                        else 
                            ++i;
                    }

                    // adjust backoff based on progress
                    if (progress) 
                        backoff_counter = 0;
                    else 
                        backoff_counter = std::min(backoff_counter + 2, MAX_BACKOFF);
                }

            private:
                /**
                 * @brief Activates next nodes in the graph when a task finishes.
                 * @param task Finished task pointer.
                 * @param queue Current thread's task queue to push new ready nodes.
                 */
                template<typename QueueType>
                void trigger_successors(SushiRuntime::Scheduler::Task* task, QueueType& queue) 
                {
                    if (!task || !task->is_graph_node())
                    {
                        return;
                    }

                    auto* node = static_cast<SushiRuntime::Graph::Node*>(task);
                    
                    int32_t count = node->inline_successor_count.load(std::memory_order_acquire);
                    int32_t max_idx = std::min(count, SushiRuntime::Graph::Node::MAX_INLINE_SUCCESSORS);
                    
                    // iterate through inline successors
                    for (int32_t i = 0; i < max_idx; ++i)
                    {
                        auto* succ = node->inline_successors[i];
                        if (succ && succ->in_degree.fetch_sub(1, std::memory_order_acq_rel) == 1) 
                        {
                            succ->add_ref();
                            queue.push(succ);
                        }
                    }

                    // iterate through spillover successors in a lock-free way
                    auto* curr = node->successor_list.load(std::memory_order_acquire);
                    while(curr) 
                    {
                        auto* succ = curr->target;
                        
                        // if all dependencies are met, push the node to the queue
                        if(succ->in_degree.fetch_sub(1, std::memory_order_acq_rel) == 1) 
                        {
                            succ->add_ref();
                            queue.push(succ);
                        }
                        
                        curr = curr->next;
                    }
                }
        };
        
    } // namespace Scheduler
} // namespace SushiRuntime
