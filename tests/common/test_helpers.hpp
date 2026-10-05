/****************************************************************/
/* test_helpers.hpp                                             */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file test_helpers.hpp
 * @brief Declares the host tasks and the USM guard that the test suites share.
 * @author Mustafa Garip
 * @see tests/common/README.md
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
         * @brief Creates a host task that records its ID into a shared list.
         * @param execution_order List the task appends to; must outlive the task.
         * @param order_mutex Mutex that guards @p execution_order; must outlive the task.
         * @param delay Wait before recording, which widens the window for ordering bugs.
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
         * @brief Canonical scheduler workload that bumps an atomic counter.
         *
         * Performs a simple increment on the host, requiring no GPU involvement.
         */
        class AtomicCounterTask : public Scheduler::Task
        {
        public:
            /**
             * @brief Constructs an AtomicCounterTask.
             * @param c Reference to the atomic counter to increment.
             */
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
         * @brief Task that records its priority into a shared list.
         *
         * Used in scheduler tests to assert that priority ordering is respected.
         */
        class PriorityTrackingTask : public Scheduler::Task
        {
        public:
            /**
             * @brief Constructs a PriorityTrackingTask.
             * @param p The priority level of this task.
             * @param order Reference to the shared list for recording execution order.
             * @param mtx Mutex protecting the shared execution order list.
             */
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
         * @brief Move-only RAII guard for a sycl::malloc_shared buffer.
         *
         * Ensures that the allocated USM memory is automatically freed upon exit.
         */
        template <typename T>
        class UsmShared
        {
        public:
            /**
             * @brief Allocates shared USM memory.
             * @param q The SYCL queue used for allocation.
             * @param count Number of elements of type T to allocate.
             */
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

            T* get() const noexcept                  { return ptr_; }
            T& operator[](size_t i) const            { return ptr_[i]; }
            size_t count() const noexcept            { return count_; }
            explicit operator bool() const noexcept  { return ptr_ != nullptr; }

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
