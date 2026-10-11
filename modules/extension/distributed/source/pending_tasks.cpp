/**************************************************************************/
/* pending_tasks.cpp                                                      */
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
 * @file pending_tasks.cpp
 * @brief Implements PendingTasks, the table of tasks out on a worker and owner of their nodes.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#include "pending_tasks.hpp"

#include <algorithm>
#include <utility>

namespace SushiRuntime
{
    namespace Distributed
    {
        PendingTasks::PendingTasks() = default;

        PendingTasks::~PendingTasks() = default;

        std::uint64_t PendingTasks::allocate_task_id() noexcept
        {
            return next_task_id_.fetch_add(1, std::memory_order_relaxed);
        }

        bool PendingTasks::insert(std::uint64_t task_id, Graph::Node& node, PeerID worker,
                                  SushiRuntime::span<const BufferHandle> outputs,
                                  std::chrono::steady_clock::time_point now)
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (closed_ || tasks_.count(task_id) != 0)
                return false;

            Entry entry;
            entry.node = &node;
            entry.worker = worker;
            entry.outputs.assign(outputs.begin(), outputs.end());
            entry.dispatched_at = now;

            const auto notes = workers_.try_emplace(worker.value).first;
            tasks_.emplace(task_id, std::move(entry));
            ++notes->second.tasks;
            size_.store(tasks_.size(), std::memory_order_relaxed);
            return true;
        }

        bool PendingTasks::acknowledge(std::uint64_t task_id, PeerID worker)
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            const auto position = tasks_.find(task_id);
            if (position == tasks_.end() || position->second.worker != worker)
                return false;

            position->second.acknowledged = true;
            return true;
        }

        void PendingTasks::note_chunk(PeerID worker, std::chrono::steady_clock::time_point now)
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            const auto notes = workers_.find(worker.value);
            if (notes != workers_.end())
                notes->second.last_chunk = std::max(notes->second.last_chunk, now);
        }

        void PendingTasks::note_heartbeat(PeerID worker, std::uint64_t oldest_held_task_id)
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            for (auto& [task_id, entry] : tasks_)
            {
                if (entry.worker != worker || !entry.acknowledged)
                    continue;

                if (task_id >= oldest_held_task_id)
                    entry.heartbeat_misses = 0;
                else if (entry.heartbeat_misses < K_LOST_RESULT_HEARTBEATS)
                    ++entry.heartbeat_misses;
            }
        }

        bool PendingTasks::expects_output(std::uint64_t task_id, PeerID worker,
                                          GlobalBufferID id, std::uint32_t version) const
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            const auto position = tasks_.find(task_id);
            if (position == tasks_.end() || position->second.worker != worker)
                return false;

            const std::vector<BufferHandle>& outputs = position->second.outputs;
            return std::any_of(outputs.begin(), outputs.end(),
                               [id, version](const BufferHandle& output)
                               {
                                   return output.id == id && output.version == version;
                               });
        }

        std::optional<PendingTask> PendingTasks::take(std::uint64_t task_id, PeerID worker)
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            const auto position = tasks_.find(task_id);
            if (position == tasks_.end() || position->second.worker != worker)
                return std::nullopt;

            return detach(position, ReapCause::Deadline);
        }

        void PendingTasks::take_for_worker(PeerID worker, std::vector<PendingTask>& out)
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            for (auto position = tasks_.begin(); position != tasks_.end();)
            {
                const auto current = position++;
                if (current->second.worker != worker)
                    continue;

                // The slot exists before the entry leaves, so a failed allocation loses no node.
                out.emplace_back();
                out.back() = detach(current, ReapCause::Deadline);
            }
        }

        void PendingTasks::take_overdue(std::chrono::steady_clock::time_point now,
                                        const OverdueLimits& limits,
                                        std::vector<PendingTask>& out)
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            for (auto position = tasks_.begin(); position != tasks_.end();)
            {
                const auto current = position++;
                const std::optional<ReapCause> cause = overdue_cause(current->second, now, limits);
                if (!cause.has_value())
                    continue;

                // The slot exists before the entry leaves, so a failed allocation loses no node.
                out.emplace_back();
                out.back() = detach(current, *cause);
            }
        }

        void PendingTasks::close() noexcept
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }

        void PendingTasks::drain(std::vector<PendingTask>& out)
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            out.reserve(out.size() + tasks_.size());
            while (!tasks_.empty())
                out.push_back(detach(tasks_.begin(), ReapCause::Deadline));
        }

        std::size_t PendingTasks::size() const noexcept
        {
            return size_.load(std::memory_order_relaxed);
        }

        std::size_t PendingTasks::count_for_worker(PeerID worker) const
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            const auto notes = workers_.find(worker.value);
            return notes == workers_.end() ? 0 : notes->second.tasks;
        }

        std::optional<ReapCause> PendingTasks::overdue_cause(
            const Entry& entry, std::chrono::steady_clock::time_point now,
            const OverdueLimits& limits) const noexcept
        {
            if (!entry.acknowledged)
            {
                if (now - entry.dispatched_at > limits.acknowledge_timeout)
                    return ReapCause::Unacknowledged;
            }
            else if (entry.heartbeat_misses >= K_LOST_RESULT_HEARTBEATS)
            {
                return ReapCause::LostResult;
            }

            if (limits.task_deadline <= std::chrono::milliseconds::zero())
                return std::nullopt;

            // insert() and detach() keep a notes entry for every worker that has a task here.
            const WorkerNotes& notes = workers_.find(entry.worker.value)->second;
            const auto restarted_at = std::max(entry.dispatched_at, notes.last_chunk);
            if (now - restarted_at > limits.task_deadline)
                return ReapCause::Deadline;

            return std::nullopt;
        }

        PendingTask PendingTasks::detach(TaskTable::iterator position, ReapCause cause) noexcept
        {
            Entry& entry = position->second;

            PendingTask task;
            task.task_id = position->first;
            task.node = entry.node;
            task.worker = entry.worker;
            task.outputs = std::move(entry.outputs);
            task.cause = cause;

            const auto notes = workers_.find(entry.worker.value);
            if (--notes->second.tasks == 0)
                workers_.erase(notes);

            tasks_.erase(position);
            size_.store(tasks_.size(), std::memory_order_relaxed);
            return task;
        }

    } // namespace Distributed
} // namespace SushiRuntime
