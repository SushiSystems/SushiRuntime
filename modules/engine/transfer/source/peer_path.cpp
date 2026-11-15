/**************************************************************************/
/* peer_path.cpp                                                          */
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
 * @file peer_path.cpp
 * @brief Implements PeerPath.
 * @see modules/engine/transfer/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/transfer/peer_path.hpp>

#include <algorithm>
#include <stdexcept>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        PeerPath::PeerPath(const SpaceQueues& queues, PeerAccess& access, TransferObserver* observer) noexcept
            : queues_(&queues), access_(&access), observer_(observer)
        {

        }

        std::string_view PeerPath::name() const noexcept
        {
            return K_NAME;
        }

        TransferPathID PeerPath::id() const noexcept
        {
            return K_ID;
        }

        bool PeerPath::connects(const Memory::MemorySpace& source, const Memory::MemorySpace& destination) const
        {
            const std::vector<MemorySpaceID>& targets = source.direct_targets();
            if (std::binary_search(targets.begin(), targets.end(), destination.id()))
                return false;
            return queues_->queue_of(destination.id()) != nullptr && access_->grant(destination.id(), source.id());
        }

        sycl::event PeerPath::copy(const TransferRequest& request, const std::vector<sycl::event>& dependencies)
        {
            sycl::queue* const queue = queues_->queue_of(request.destination);
            if (queue == nullptr)
                throw std::runtime_error("peer: the destination space has no queue.");
            const sycl::event done = queue->memcpy(request.to, request.from, request.bytes, dependencies);
            if (observer_ != nullptr)
                observer_->copied(TransferRecord{name(), request.source, request.destination, request.bytes, K_ID});
            return done;
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
