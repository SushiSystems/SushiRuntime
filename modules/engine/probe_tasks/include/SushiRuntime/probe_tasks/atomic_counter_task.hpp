/**************************************************************************/
/* atomic_counter_task.hpp                                                */
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
 * @file atomic_counter_task.hpp
 * @brief Declares the scheduler task that bumps an atomic counter.
 * @author Mustafa Garip
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <optional>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/task/task.hpp>
#include <SushiRuntime/task/task_types.hpp>

namespace SushiRuntime
{
    namespace Test
    {
        /**
         * @brief Canonical scheduler workload that bumps an atomic counter.
         *
         * Performs a simple increment on the host, requiring no GPU involvement.
         */
        class AtomicCounterTask : public Internal::Scheduler::Task
        {
        public:
            /**
             * @brief Constructs an AtomicCounterTask.
             * @param c Reference to the atomic counter to increment.
             */
            explicit AtomicCounterTask(std::atomic<size_t>& c)
                : counter_(c) {}

            const char* name() const noexcept override { return "AtomicCounterTask"; }

            ExecutionMode execution_mode() const override { return ExecutionMode::Host; }

            sycl::event execute_host(sycl::queue&, const std::vector<sycl::event>&) override
            {
                counter_.fetch_add(1, std::memory_order_relaxed);
                return {};
            }

            std::optional<sushi_ptr<Task>> split() override { return std::nullopt; }
            bool is_splittable() const override             { return false; }

        private:
            std::atomic<size_t>& counter_;
        };
    } // namespace Test
} // namespace SushiRuntime
