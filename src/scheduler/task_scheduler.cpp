#include "task_scheduler.h"
#include "../common/logging/logger.h"

namespace SushiBLAS 
{
    TaskScheduler::TaskScheduler() 
    {
        // Workers are initialized lazily in submit()
    }

    TaskScheduler::~TaskScheduler() 
    {
        stop_flag = true;
        cv.notify_all();
        for (auto& thread : workers) 
        {
            if (thread.joinable()) thread.join();
        }
    }

    TaskScheduler& TaskScheduler::instance() 
    {
        static TaskScheduler inst;
        return inst;
    }

    void TaskScheduler::submit(Task task) 
    {
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            
            // Lazy Worker Initialization
            if (workers.empty() && !stop_flag) 
            {
                unsigned int num_threads = std::thread::hardware_concurrency();
                if (num_threads == 0) num_threads = 4;

                SB_LOG_INFO("Initializing TaskScheduler with {} threads (Lazy Initialization)", num_threads);

                for (unsigned int i = 0; i < num_threads; ++i) 
                {
                    workers.emplace_back(&TaskScheduler::worker_thread, this);
                }
            }

            global_queue.push_back(std::move(task));
        }
        cv.notify_one();
    }

    void TaskScheduler::synchronize() 
    {
        // TODO: Simple polling for now
        while (true) 
        {
            if (global_queue.empty() && active_tasks.load() == 0) 
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(1));
        }
    }

    bool TaskScheduler::are_dependencies_met(const Task& task) const 
    {
        for (const auto& dep : task.dependencies) 
        {
            if (!dep->is_completed()) return false;
        }
        return true;
    }

    void TaskScheduler::worker_thread() 
    {
        while (!stop_flag) 
        {
            Task task;
            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                cv.wait(lock, [this] { return stop_flag || !global_queue.empty(); });

                if (stop_flag && global_queue.empty()) return;

                // TODO: Simple check for dependencies
                bool found_ready = false;
                for (auto it = global_queue.begin(); it != global_queue.end(); ++it) 
                {
                    if (are_dependencies_met(*it)) 
                    {
                        task = std::move(*it);
                        global_queue.erase(it);
                        found_ready = true;
                        break;
                    }
                }

                if (!found_ready) 
                {
                    lock.unlock();
                    std::this_thread::yield();
                    continue;
                }
                
                active_tasks.fetch_add(1, std::memory_order_relaxed);
            }

            if (task.work) 
            {
                task.work();
            }
            
            active_tasks.fetch_sub(1, std::memory_order_relaxed);

            if (task.signal_event) 
            {
                task.signal_event->record(nullptr);
            }
        }
    }
} // namespace SushiBLAS
