/**************************************************************************/
/* task.hpp                                                               */
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

#include <atomic>
#include <cstdint>
#include <optional>
#include <functional>
#include <sycl/sycl.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/core/ref_counted.hpp>
#include <SushiRuntime/scheduler/ready_sink.hpp>

namespace SushiRuntime 
{
    namespace Scheduler
    {
        /**
         * @brief Interface for tasks that can be split for load balancing.
         */
        class SUSHIRUNTIME_API Task : public Core::RefCounted 
        {
        public:
            enum class ExecutionMode { KERNEL, HOST };

            /** @brief Default constructor. */
            Task() = default;
            
            /** @brief Constructor with name. */
            Task(const char* name) : name_(name) {}

            /**
             * @brief Returns the name of the task.
             * @return const char* Pointer to the task name.
             */
            virtual const char* get_name() const { return name_; }

            /**
             * @brief Sets the name of the task.
             * @param name Pointer to the task name (should be persistent, e.g., string literal).
             */
            virtual void set_name(const char* name) { name_ = name; }

            /**
             * @brief Returns the execution mode of this task.
             * @return ExecutionMode::KERNEL by default.
             */
            virtual ExecutionMode get_execution_mode() const { return ExecutionMode::KERNEL; }

            /**
             * @brief Indicates whether the task is a graph node managed by the DAG layer.
             *
             * This gives the scheduler a safe way to distinguish graph nodes from other
             * task types without relying on unchecked casts.
             */
            virtual bool is_graph_node() const noexcept { return false; }

            /**
             * @brief Releases whatever this task's completion unblocked.
             *
             * Called once by the poller when the task is reaped, on the reaping
             * worker's thread. A task that nothing depends on does nothing here; a
             * graph node walks its successors, satisfies one dependency on each, and
             * hands the sink the ones that have no dependencies left. The scheduler
             * never has to know which kind it reaped.
             *
             * @param sink Receives each task this completion made runnable.
             */
            virtual void on_complete(ReadySink& /*sink*/) {}

            
            /**
             * @brief Main execution logic for the task (Kernel Mode).
             * @param h SYCL handler to submit work.
             */
            virtual void execute(sycl::handler& /*h*/) {}

            /**
             * @brief Main execution logic for the task (Host Mode).
             * @param q SYCL queue to submit work to.
             * @param deps List of event dependencies.
             * @return sycl::event The event returned by the library call.
             */
            virtual sycl::event execute_host(sycl::queue& /*q*/, const std::vector<sycl::event>& /*deps*/) { return {}; }

            /**
             * @brief Whether this task submits SYCL device work and so needs a queue.
             *
             * The scheduler reads this to decide between two host-execution paths:
             * true (the safe default for every task) routes through execute_host with
             * a real queue, so a host_work lambda that itself submits a kernel is
             * dispatched on the queue owning its data; false marks a pure-CPU task the
             * scheduler runs natively via execute_native(), skipping queue acquisition
             * entirely. Only override to false when the work provably touches no SYCL
             * device API.
             */
            virtual bool submits_device_work() const noexcept { return true; }

            /**
             * @brief Native host execution: plain C++ run directly on the worker thread.
             *
             * Used only when submits_device_work() is false. The scheduler invokes
             * this without a queue and reports an empty event, so completion flows
             * through the same successor-release path as any other task. Default is a
             * no-op for task types that never opt into the native path.
             */
            virtual void execute_native() {}

            /**
             * @brief Splits the task into two. Returns the new task part.
             * @return optional sushi_ptr to the new task part.
             */
            virtual std::optional<sushi_ptr<Task>> split() = 0;

            /**
             * @brief Determines if the task is still large enough to be split.
             * @return true if the task can be split.
             */
            virtual bool is_splittable() const = 0;
            
            /**
             * @brief Returns the priority of the task. Higher value = higher priority.
             * @return int 0 by default.
             */
            virtual int get_priority() const noexcept { return 0; }
            
            virtual ~Task() = default;

        protected:
            /** @brief Task name (0-allocation). */
            const char* name_ = "unnamed_task";
        };

        /**
         * @brief A splittable work-stealing task over a contiguous index range.
         *
         * Represents work for [start, end) and can donate the lower half to a
         * thief via split(). The start index is atomic so a split and an execute
         * racing on the same task are both safe: each sees a consistent range.
         * Used by the graph layer to expose large parallel_for ranges as
         * steal-friendly units.
         */
        class SUSHIRUNTIME_API RangeTask : public Task
        {
            private:
                /** @brief Atomic start index of the current range. */
                std::atomic<size_t> start_;
                /** @brief Fixed end index of the range. */
                size_t end_;
                /** @brief The function to execute for each sub-range. */
                std::function<void(size_t, size_t, sycl::handler&)> kernel_func_;

            public:
                /** @brief Creates a new range task. */
                template <typename Func>
                RangeTask(size_t start, size_t end, Func func, const char* name = "range_task")
                    : Task(name), start_(start), end_(end), kernel_func_(std::move(func)) {}

                /** @brief Executes the task for the current valid range. */
                void execute(sycl::handler& h) override 
                {
                    size_t s = start_.load(std::memory_order_acquire);
                    if (s < end_) kernel_func_(s, end_, h);
                }

                /**
                 * @brief Splits the range task at the midpoint.
                 * 
                 * Uses atomic compare-exchange to hand over part of the work.
                 * 
                 * @return optional sushi_ptr to the new range task.
                 */
                std::optional<sushi_ptr<Task>> split() override 
                {
                    size_t s = start_.load(std::memory_order_acquire);
                    size_t mid = s + (end_ - s) / 2;

                    // prevent splitting if the range is too small
                    if (mid <= s || (end_ - mid) < 1024) return std::nullopt;

                    if (start_.compare_exchange_strong(s, mid, std::memory_order_acq_rel)) 
                        return make_sushi<RangeTask>(s, mid, kernel_func_);
                    
                    return std::nullopt;
                }

                /** @brief Returns true if the range is large enough to split. */
                bool is_splittable() const override 
                {
                    return (end_ - start_.load()) > 1024;
                }
        };

    } // namespace Scheduler
} // namespace SushiRuntime
