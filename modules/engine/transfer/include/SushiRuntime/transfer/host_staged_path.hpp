/**************************************************************************/
/* host_staged_path.hpp                                                   */
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
 * @file host_staged_path.hpp
 * @brief Declares HostStagedPath, the copy between two spaces through pinned host memory.
 * @see modules/engine/transfer/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <string_view>
#include <vector>

#include <SushiRuntime/transfer/space_queues.hpp>
#include <SushiRuntime/transfer/staging_ring.hpp>
#include <SushiRuntime/transfer/transfer_observer.hpp>
#include <SushiRuntime/transfer/transfer_path.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Copies between two spaces that have no direct or peer path.
         *
         * A device-to-device copy moves one ring slot of bytes at a time, source to slot on the
         * source's queue, then slot to destination on the destination's queue, the two legs of
         * neighbouring chunks overlapping. A copy to or from the host space is one memcpy.
         * copy() blocks until each chunk has reached its slot.
         */
        class HostStagedPath final : public TransferPath
        {
            public:
                /**
                 * @param queues Names the queue of a space; a space with none is read as host memory; must outlive the path.
                 * @param ring Holds the staging slots; must outlive the path.
                 * @param observer Hears each copy; null for none; must outlive the path.
                 */
                HostStagedPath(const SpaceQueues& queues, StagingRing& ring,
                               TransferObserver* observer = nullptr) noexcept;

                /// @brief Name of the path, for logs and the profile file.
                static constexpr std::string_view K_NAME = "host_staged";

                /// @brief Identity of the path; every comparison and price keys on it.
                static constexpr TransferPathID K_ID = TransferPathID::named(K_NAME);

                [[nodiscard]] std::string_view name() const noexcept override;

                [[nodiscard]] TransferPathID id() const noexcept override;

                /**
                 * @brief Returns whether the copy needs a device queue and two spaces that are not both host.
                 *
                 * A host space connects to a space that has a queue; two host spaces connect to nothing.
                 */
                [[nodiscard]] bool connects(const Memory::MemorySpace& source,
                                            const Memory::MemorySpace& destination) const override;

                /** @throws std::runtime_error when neither space has a queue, or the ring has no slots. */
                [[nodiscard]] sycl::event copy(const TransferRequest& request,
                                               const std::vector<sycl::event>& dependencies) override;

            private:
                /// @brief Names the queue of a space.
                const SpaceQueues* queues_;

                /// @brief Holds the staging slots.
                StagingRing* ring_;

                /// @brief Hears each copy; null for none.
                TransferObserver* observer_;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
