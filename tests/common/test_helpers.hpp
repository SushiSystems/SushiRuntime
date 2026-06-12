/**************************************************************************/
/* test_helpers.hpp                                                       */
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

/**
 * @file test_helpers.hpp
 * @brief Shared test utilities to remove duplication across the test suites.
 *
 * Collects the small building blocks that were previously copy-pasted into
 * individual test files: a host task that records execution order, a host task
 * that bumps an atomic counter, and an RAII wrapper for USM allocations so a
 * failing assertion never leaks device memory.
 */

#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#include <sycl/sycl.hpp>
#include <SushiRuntime/graph/task_types.hpp>
#include <SushiRuntime/scheduler/task.hpp>

namespace SushiRuntime
{
    namespace Test
    {
        /**
         * @brief Host work lambda that records its id into a shared order list.
         *
         * Used by graph/integration tests to assert a topological execution
         * order. The optional delay widens the window for ordering bugs to
         * surface when several tasks could otherwise appear simultaneous.
         */
        inline auto make_order_recorder(int task_id,
                                        std::vector<int>& execution_order,
                                        std::mutex& order_mutex,
                                        std::chrono::milliseconds delay = std::chrono::milliseconds(0))
        {
            return [task_id, &execution_order, &order_mutex, delay]
                   (sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
            {
                if (delay.count() > 0)
                    std::this_thread::sleep_for(delay);

                std::lock_guard<std::mutex> lock(order_mutex);
                execution_order.push_back(task_id);
                return {};
            };
        }

        /**
         * @brief Lightweight host task that increments an atomic counter.
         *
         * The canonical scheduler workload: no GPU involvement, so it isolates
         * the scheduler's dispatch path from kernel execution cost.
         */
        class AtomicCounterTask : public Scheduler::Task
        {
        public:
            explicit AtomicCounterTask(std::atomic<size_t>& c)
                : Task("AtomicCounterTask"), counter_(c) {}

            ExecutionMode get_execution_mode() const override { return ExecutionMode::HOST; }

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

        /**
         * @brief Host task that records its priority into a shared order list.
         *
         * Used by scheduler tests to verify high-priority tasks bypass the
         * normal queue ordering.
         */
        class PriorityTrackingTask : public Scheduler::Task
        {
        public:
            PriorityTrackingTask(int p, std::vector<int>& order, std::mutex& mtx)
                : Task("PriorityTask"), priority_(p), execution_order_(order), order_mutex_(mtx) {}

            int get_priority() const noexcept override        { return priority_; }
            ExecutionMode get_execution_mode() const override { return ExecutionMode::HOST; }

            sycl::event execute_host(sycl::queue&, const std::vector<sycl::event>&) override
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                std::lock_guard<std::mutex> lock(order_mutex_);
                execution_order_.push_back(priority_);
                return {};
            }

            std::optional<sushi_ptr<Task>> split() override { return std::nullopt; }
            bool is_splittable() const override             { return false; }

        private:
            int priority_;
            std::vector<int>& execution_order_;
            std::mutex& order_mutex_;
        };

        /**
         * @brief RAII guard for a sycl::malloc_shared<T> allocation.
         *
         * Frees on scope exit, so a failing GTest assertion does not leak the
         * device buffer. Move-only.
         */
        template <typename T>
        class UsmShared
        {
        public:
            UsmShared(sycl::queue& q, size_t count)
                : queue_(&q), ptr_(sycl::malloc_shared<T>(count, q)), count_(count) {}

            ~UsmShared() { reset(); }

            UsmShared(const UsmShared&) = delete;
            UsmShared& operator=(const UsmShared&) = delete;

            UsmShared(UsmShared&& other) noexcept
                : queue_(other.queue_), ptr_(other.ptr_), count_(other.count_)
            {
                other.ptr_ = nullptr;
            }

            UsmShared& operator=(UsmShared&& other) noexcept
            {
                if (this != &other)
                {
                    reset();
                    queue_ = other.queue_;
                    ptr_   = other.ptr_;
                    count_ = other.count_;
                    other.ptr_ = nullptr;
                }
                return *this;
            }

            T* get() const noexcept       { return ptr_; }
            T& operator[](size_t i) const { return ptr_[i]; }
            size_t count() const noexcept { return count_; }
            explicit operator bool() const noexcept { return ptr_ != nullptr; }

        private:
            void reset()
            {
                if (ptr_ && queue_)
                    sycl::free(ptr_, *queue_);
                ptr_ = nullptr;
            }

            sycl::queue* queue_ = nullptr;
            T* ptr_ = nullptr;
            size_t count_ = 0;
        };

    } // namespace Test
} // namespace SushiRuntime
