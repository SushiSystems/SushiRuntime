/**************************************************************************/
/* recording_task_migrator.hpp                                            */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file recording_task_migrator.hpp
 * @brief Declares RecordingTaskMigrator, a TaskMigrator that keeps every migration it is asked for.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#pragma once

#include <cstddef>
#include <vector>

#include <SushiRuntime/scheduler/rebalancer_interfaces.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        /**
         * @brief One migration a RecordingTaskMigrator received.
         */
        struct MigrationRequest
        {
            int from_worker_id = -1;
            size_t count = 0;

            bool operator==(const MigrationRequest& other) const
            {
                return from_worker_id == other.from_worker_id && count == other.count;
            }
        };

        /**
         * @brief Keeps every migration request in arrival order and moves no task.
         */
        class RecordingTaskMigrator final : public TaskMigrator
        {
            public:
                /**
                 * @brief Records one migration request.
                 * @param from_worker_id Source worker index.
                 * @param count          Maximum number of tasks to move.
                 */
                void migrate_tasks(int from_worker_id, size_t count) override
                {
                    requests_.push_back(MigrationRequest{from_worker_id, count});
                }

                /** @return Every request received, oldest first. */
                const std::vector<MigrationRequest>& requests() const
                {
                    return requests_;
                }

            private:
                std::vector<MigrationRequest> requests_;
        };
    } // namespace Internal::Scheduler
} // namespace SushiRuntime
