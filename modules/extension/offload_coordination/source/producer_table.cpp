/**************************************************************************/
/* producer_table.cpp                                                     */
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
 * @file producer_table.cpp
 * @brief Implements ProducerTable, the node that wrote the current version of each handle.
 * @author Mustafa Garip
 * @see modules/extension/offload_coordination/README.md
 */

#include <SushiRuntime/offload_coordination/producer_table.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        void ProducerTable::record_remote(SushiRuntime::span<const BufferHandle> outputs,
                                          Internal::Graph::Node* node, std::uint64_t task_id,
                                          PeerID worker)
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            for (const BufferHandle& output : outputs)
                store(output.id, ProducerRecord{output.version, node, task_id, worker});
        }

        void ProducerTable::record_local(SushiRuntime::span<const BufferHandle> outputs,
                                         Internal::Graph::Node* node)
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            for (const BufferHandle& output : outputs)
                store(output.id, ProducerRecord{output.version, node, 0, PeerID{}});
        }

        void ProducerTable::settle(std::uint64_t task_id,
                                   SushiRuntime::span<const BufferHandle> outputs) noexcept
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            for (const BufferHandle& output : outputs)
            {
                const auto found = records_.find(output.id);
                if (found == records_.end())
                    continue;

                ProducerRecord& record = found->second;
                if (record.task_id == task_id && record.version == output.version)
                    record.holder = PeerID{};
            }
        }

        void ProducerTable::lose(PeerID worker, std::vector<LostInstance>& out)
        {
            out.clear();
            if (worker.value == 0)
                return;

            const std::lock_guard<std::mutex> lock(mutex_);
            for (auto& entry : records_)
            {
                ProducerRecord& record = entry.second;
                if (record.holder.value != worker.value)
                    continue;

                out.push_back(LostInstance{entry.first, record});
                record.holder = PeerID{};
            }
        }

        std::optional<ProducerRecord> ProducerTable::producer_of(GlobalHandle handle) const
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            const auto found = records_.find(handle);
            if (found == records_.end())
                return std::nullopt;
            return found->second;
        }

        void ProducerTable::forget(GlobalHandle handle) noexcept
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            records_.erase(handle);
        }

        void ProducerTable::store(GlobalHandle handle, const ProducerRecord& record)
        {
            const auto inserted = records_.emplace(handle, record);
            if (!inserted.second && inserted.first->second.version <= record.version)
                inserted.first->second = record;
        }

    } // namespace Distributed
} // namespace SushiRuntime
