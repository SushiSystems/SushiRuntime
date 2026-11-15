/**************************************************************************/
/* host_staged_path.cpp                                                   */
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
 * @file host_staged_path.cpp
 * @brief Implements HostStagedPath.
 * @see modules/engine/transfer/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/transfer/host_staged_path.hpp>

#include <algorithm>
#include <cstddef>
#include <stdexcept>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        HostStagedPath::HostStagedPath(const SpaceQueues& queues, StagingRing& ring,
                                       TransferObserver* observer) noexcept
            : queues_(&queues), ring_(&ring), observer_(observer)
        {

        }

        std::string_view HostStagedPath::name() const noexcept
        {
            return K_NAME;
        }

        TransferPathID HostStagedPath::id() const noexcept
        {
            return K_ID;
        }

        bool HostStagedPath::connects(const Memory::MemorySpace& source,
                                      const Memory::MemorySpace& destination) const
        {
            const bool source_host = source.is_host_memory();
            const bool destination_host = destination.is_host_memory();
            if (source_host && destination_host)
                return false;
            if (source_host)
                return queues_->queue_of(destination.id()) != nullptr;
            if (destination_host)
                return queues_->queue_of(source.id()) != nullptr;
            return queues_->queue_of(source.id()) != nullptr && queues_->queue_of(destination.id()) != nullptr;
        }

        sycl::event HostStagedPath::copy(const TransferRequest& request, const std::vector<sycl::event>& dependencies)
        {
            sycl::queue* const from_queue = queues_->queue_of(request.source);
            sycl::queue* const to_queue = queues_->queue_of(request.destination);
            if (from_queue == nullptr && to_queue == nullptr)
                throw std::runtime_error("host staged: neither space has a queue.");

            sycl::event done;
            if (from_queue == nullptr || to_queue == nullptr || request.bytes == 0)
            {
                sycl::queue* const device_queue = to_queue != nullptr ? to_queue : from_queue;
                done = device_queue->memcpy(request.to, request.from, request.bytes, dependencies);
            }
            else
            {
                const std::size_t slot_bytes = ring_->shape().slot_bytes;
                const auto* const from = static_cast<const std::byte*>(request.from);
                auto* const to = static_cast<std::byte*>(request.to);
                std::vector<sycl::event> after_previous_chunk;
                for (std::size_t offset = 0; offset < request.bytes; offset += slot_bytes)
                {
                    const std::size_t chunk = std::min(slot_bytes, request.bytes - offset);
                    const StagingSlot slot = ring_->acquire();
                    try
                    {
                        from_queue->memcpy(slot.data, from + offset, chunk, dependencies).wait();
                        done = to_queue->memcpy(to + offset, slot.data, chunk, after_previous_chunk);
                    }
                    catch (...)
                    {
                        ring_->retire(slot, sycl::event{});
                        throw;
                    }
                    ring_->retire(slot, done);
                    after_previous_chunk.assign(1, done);
                }
            }
            if (observer_ != nullptr)
                observer_->copied(TransferRecord{name(), request.source, request.destination, request.bytes, K_ID});
            return done;
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
