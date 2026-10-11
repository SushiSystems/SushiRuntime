/**************************************************************************/
/* pending_tasks.hpp                                                      */
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
 * @file pending_tasks.hpp
 * @brief Declares PendingTasks, the table of tasks out on a worker and owner of their nodes.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/distributed/buffer_handle.hpp>
#include <SushiRuntime/distributed/i_transport.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph { struct Node; }

    namespace Distributed
    {
        /** @brief Holds the heartbeats that must miss a task before its result counts as lost. */
        inline constexpr std::uint32_t K_LOST_RESULT_HEARTBEATS = 2;

        /**
         * @brief Names why take_overdue() removed a task.
         */
        enum class ReapCause : std::uint8_t
        {
            Deadline,       /**< The task deadline passed since dispatch or the last chunk. */
            Unacknowledged, /**< No TASK_ACCEPTED came within the acknowledgement limit. */
            LostResult,     /**< The worker's heartbeats stopped listing an accepted task. */
        };

        /**
         * @brief Holds one task removed from the table; whoever receives it owns the node.
         *
         * The receiver holds the scheduling reference try_offload() took and must pass the
         * node to the sink exactly once. Before a resubmit it bumps @ref outputs in the
         * registry, so chunks still in flight fail the version fence.
         */
        struct PendingTask
        {
            /** @brief Holds the master's id for the task. */
            std::uint64_t task_id = 0;

            /** @brief Holds the node; never null in a returned task. */
            Internal::Graph::Node* node = nullptr;

            /** @brief Holds the worker the task was dispatched to. */
            PeerID worker;

            /** @brief Holds the output handles as dispatched: id, bumped version and size. */
            std::vector<BufferHandle> outputs;

            /** @brief Holds why take_overdue() removed it; Deadline from the other removals. */
            ReapCause cause = ReapCause::Deadline;
        };

        /**
         * @brief Holds the limits take_overdue() applies.
         */
        struct OverdueLimits
        {
            /** @brief Bounds the time since dispatch or the worker's last chunk; 0 sets none. */
            std::chrono::milliseconds task_deadline{0};

            /** @brief Bounds the wait for TASK_ACCEPTED: the liveness timeout. */
            std::chrono::milliseconds acknowledge_timeout{5000};
        };

        /**
         * @brief Owns every offloaded node from insert() until one removal returns it.
         *
         * take(), take_for_worker(), take_overdue() and drain() are the only ways out, and
         * each entry leaves through exactly one of them, once. The table owns one mutex, held
         * only inside its members: it calls no other brick, no transport and no sink, and
         * never reads or writes a node. Any thread may call any member.
         */
        class SUSHIRUNTIME_API PendingTasks final
        {
            public:
                /** @brief Builds an open, empty table whose first task id is 1. */
                PendingTasks();

                /** @brief Destroys the table. */
                ~PendingTasks();

                PendingTasks(const PendingTasks&) = delete;
                PendingTasks& operator=(const PendingTasks&) = delete;
                PendingTasks(PendingTasks&&) = delete;
                PendingTasks& operator=(PendingTasks&&) = delete;

                /** @brief Returns the next task id; ids rise by one and are never 0. */
                [[nodiscard]] std::uint64_t allocate_task_id() noexcept;

                /**
                 * @brief Takes ownership of a node that is about to be dispatched.
                 *
                 * On true the caller must not touch the node again: a removal on another
                 * thread may already have handed it on. On false the caller keeps it.
                 * @param outputs Copied; the versions are those the dispatch frame carries.
                 * @return False once close() was called, or when @p task_id is in the table.
                 */
                [[nodiscard]] bool insert(std::uint64_t task_id, Internal::Graph::Node& node, PeerID worker,
                                          SushiRuntime::span<const BufferHandle> outputs,
                                          std::chrono::steady_clock::time_point now);

                /**
                 * @brief Records TASK_ACCEPTED for a task.
                 * @return False when the task is not pending on @p worker; nothing changes.
                 */
                bool acknowledge(std::uint64_t task_id, PeerID worker);

                /** @brief Restarts the deadline of every task on a worker; called per chunk. */
                void note_chunk(PeerID worker, std::chrono::steady_clock::time_point now);

                /**
                 * @brief Records a HEARTBEAT: every acknowledged task of the worker with an id
                 *        below @p oldest_held_task_id counts one miss, every other is cleared.
                 *
                 * A task with K_LOST_RESULT_HEARTBEATS misses in a row is overdue as LostResult.
                 */
                void note_heartbeat(PeerID worker, std::uint64_t oldest_held_task_id);

                /**
                 * @brief Returns whether a task still pending on @p worker was dispatched with
                 *        this output at exactly this version; the first half of the fence.
                 */
                [[nodiscard]] bool expects_output(std::uint64_t task_id, PeerID worker,
                                                  GlobalBufferID id, std::uint32_t version) const;

                /**
                 * @brief Removes one task, for its result or to undo a dispatch that failed.
                 * @return Nothing when the id is not pending on @p worker: a late or foreign
                 *         result, or a task a reaper already took. The caller then changes no
                 *         count and touches no node.
                 */
                [[nodiscard]] std::optional<PendingTask> take(std::uint64_t task_id, PeerID worker);

                /**
                 * @brief Removes every task of a lost worker and forgets what was noted for it.
                 * @param out Receives the tasks, appended.
                 */
                void take_for_worker(PeerID worker, std::vector<PendingTask>& out);

                /**
                 * @brief Removes every task past a limit and sets its cause.
                 *
                 * Unacknowledged and LostResult apply whatever the task deadline is.
                 * @param out Receives the tasks, appended.
                 */
                void take_overdue(std::chrono::steady_clock::time_point now,
                                  const OverdueLimits& limits, std::vector<PendingTask>& out);

                /** @brief Refuses every later insert(); the first step of the coordinator stop. */
                void close() noexcept;

                /**
                 * @brief Removes every task left; called after close() and after the monitor
                 *        and the transport have stopped.
                 * @param out Receives the tasks, appended.
                 */
                void drain(std::vector<PendingTask>& out);

                /** @brief Returns the number of tasks in the table; reads an atomic. */
                [[nodiscard]] std::size_t size() const noexcept;

                /** @brief Returns the number of tasks out on one worker. */
                [[nodiscard]] std::size_t count_for_worker(PeerID worker) const;

            private:
                /** @brief Holds one task out on a worker. */
                struct Entry
                {
                    /** @brief The node the table owns until a removal returns it. */
                    Internal::Graph::Node* node = nullptr;

                    /** @brief The worker the task was dispatched to. */
                    PeerID worker;

                    /** @brief The output handles as dispatched. */
                    std::vector<BufferHandle> outputs;

                    /** @brief The time insert() was given. */
                    std::chrono::steady_clock::time_point dispatched_at;

                    /** @brief Heartbeats in a row that did not list the task. */
                    std::uint32_t heartbeat_misses = 0;

                    /** @brief Whether TASK_ACCEPTED arrived. */
                    bool acknowledged = false;
                };

                /** @brief Holds what the table notes for a worker that has a task out. */
                struct WorkerNotes
                {
                    /** @brief The number of tasks of the worker in the table. */
                    std::size_t tasks = 0;

                    /** @brief The time of the last chunk exchanged with the worker. */
                    std::chrono::steady_clock::time_point last_chunk;
                };

                /** @brief Maps a task id to its entry. */
                using TaskTable = std::unordered_map<std::uint64_t, Entry>;

                /** @brief Returns the limit an entry is past, if any; called under the mutex. */
                [[nodiscard]] std::optional<ReapCause> overdue_cause(
                    const Entry& entry, std::chrono::steady_clock::time_point now,
                    const OverdueLimits& limits) const noexcept;

                /** @brief Erases an entry and returns its task; called under the mutex. */
                [[nodiscard]] PendingTask detach(TaskTable::iterator position,
                                                 ReapCause cause) noexcept;

                /// @brief Guards the table; never held across a call out of this class.
                mutable std::mutex mutex_;

                /// @brief The id allocate_task_id() returns next.
                std::atomic<std::uint64_t> next_task_id_{1};

                /// @brief The number of entries, for size().
                std::atomic<std::size_t> size_{0};

                /// @brief Set by close().
                bool closed_ = false;

                /// @brief The tasks out on a worker, by id.
                TaskTable tasks_;

                /// @brief Notes by the value of a PeerID; one per worker with a task here.
                std::unordered_map<std::uint32_t, WorkerNotes> workers_;
        };

    } // namespace Distributed
} // namespace SushiRuntime
