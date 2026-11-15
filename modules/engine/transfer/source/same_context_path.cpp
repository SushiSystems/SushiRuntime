/**************************************************************************/
/* same_context_path.cpp                                                  */
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
 * @file same_context_path.cpp
 * @brief Implements SameContextPath.
 * @see modules/engine/transfer/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/transfer/same_context_path.hpp>

#include <algorithm>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/sycl/error_translation.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        SameContextPath::SameContextPath(const SpaceQueues& queues, TransferObserver* observer) noexcept
            : queues_(&queues), observer_(observer)
        {

        }

        std::string_view SameContextPath::name() const noexcept
        {
            return K_NAME;
        }

        TransferPathID SameContextPath::id() const noexcept
        {
            return K_ID;
        }

        bool SameContextPath::connects(const Memory::MemorySpace& source,
                                       const Memory::MemorySpace& destination) const
        {
            const std::vector<MemorySpaceID>& targets = source.direct_targets();
            return std::binary_search(targets.begin(), targets.end(), destination.id()) &&
                   queues_->queue_of(destination.id()) != nullptr;
        }

        Result<sycl::event> SameContextPath::copy(const TransferRequest& request,
                                                  const std::vector<sycl::event>& dependencies)
        {
            sycl::queue* const queue = queues_->queue_of(request.destination);
            if (queue == nullptr)
                return make_error(Error{ErrorCode::InvalidArgument, "same_context: the destination space has no queue."});
            sycl::event done;
            const Result<void> submitted = Internal::SYCL::translate_exceptions([&]()
            {
                done = queue->memcpy(request.to, request.from, request.bytes, dependencies);
            });
            if (!submitted)
                return make_error(Error{submitted.error()});
            if (observer_ != nullptr)
                observer_->copied(TransferRecord{name(), request.source, request.destination, request.bytes, K_ID});
            return done;
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
