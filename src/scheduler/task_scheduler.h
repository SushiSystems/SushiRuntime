#pragma once

#include <functional>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

#include "../core/sushi_ptr.h"
#include "../event/event.h"

namespace SushiBLAS 
{
    struct Task 
    {
        std::function<void()> work;
        sushi_ptr<Event> signal_event;
        std::vector<sushi_ptr<Event>> dependencies;

        Task() = default;
        Task(std::function<void()> w, sushi_ptr<Event> sig = nullptr) 
            : work(std::move(w)), signal_event(std::move(sig)) {}
    };

    class TaskScheduler 
    {
        private:
            std::vector<std::thread> workers;
            std::deque<Task> global_queue;
            
            std::mutex queue_mutex;
            std::condition_variable cv;
            
            std::atomic<bool> stop_flag{false};
            std::atomic<int> active_tasks{0};

            TaskScheduler();
            ~TaskScheduler();

        public:
            static TaskScheduler& instance();

            void submit(Task task);
            
            // Wait for all current tasks in the scheduler to finish
            // (Simplified version: blocks until queue is empty and workers are idle)
            void synchronize();

        private:
            void worker_thread();
            bool are_dependencies_met(const Task& task) const;
    };
} // namespace SushiBLAS
