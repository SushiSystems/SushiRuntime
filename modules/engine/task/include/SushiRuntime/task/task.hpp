/**************************************************************************/
/* task.hpp                                                               */
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
 * @file task.hpp
 * @brief Declares Task, the unit the scheduler dispatches.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#pragma once

#include <optional>
#include <vector>
#include <sycl/sycl.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/core/ref_counted.hpp>
#include <SushiRuntime/task/ready_sink.hpp>

namespace SushiRuntime
{
    namespace Scheduler
    {
        /**
         * @brief Interface for tasks that can be split for load balancing.
         */
        class SR_TYPE_API Task : public Core::RefCounted
        {
        public:
            enum class ExecutionMode { KERNEL, HOST };

            /** @brief Default constructor. */
            Task() = default;

            /**
             * @brief Returns the label a failure report names this task by.
             * @return A string that outlives the task; "unnamed_task" unless a derived type
             *         holds a label of its own.
             */
            virtual const char* get_name() const noexcept { return "unnamed_task"; }

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
             * @param sink Receives each task this completion made runnable.
             * @see modules/engine/scheduler/README.md
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
             * @brief Returns whether this task submits SYCL device work and so needs a queue.
             * @see modules/engine/scheduler/README.md
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
        };

    } // namespace Scheduler
} // namespace SushiRuntime
