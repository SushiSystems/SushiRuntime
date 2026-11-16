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

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/transfer/copy_submitter.hpp>
#include <SushiRuntime/transfer/space_queues.hpp>
#include <SushiRuntime/transfer/staging_rings.hpp>
#include <SushiRuntime/transfer/transfer_observer.hpp>
#include <SushiRuntime/transfer/transfer_path.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Copies between two spaces that have no direct or peer path.
         *
         * A device-to-device copy moves one slot of the source context's ring at a time, source
         * to slot on the source's queue, then slot to destination on the destination's queue;
         * copy() submits every leg and waits for none. A host copy is one memcpy.
         */
        class SUSHIRUNTIME_API HostStagedPath final : public TransferPath
        {
            public:
                /**
                 * @param queues Names the queue of a space; a space with none is read as host memory; must outlive the path.
                 * @param rings Holds the staging ring of each context; must outlive the path.
                 * @param submitter Submits each leg; must outlive the path.
                 * @param observer Hears each copy; null for none; must outlive the path.
                 */
                HostStagedPath(const SpaceQueues& queues, const StagingRings& rings, CopySubmitter& submitter,
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

                /** @return An error when neither space has a queue, the ring has no slots or a device call fails. */
                [[nodiscard]] Result<sycl::event> copy(const TransferRequest& request,
                                               const std::vector<sycl::event>& dependencies) override;

            private:
                /// @brief Names the queue of a space.
                const SpaceQueues* queues_;

                /// @brief Holds the staging ring of each context.
                const StagingRings* rings_;

                /// @brief Hears each copy; null for none.
                TransferObserver* observer_;

                /// @brief Submits each leg.
                CopySubmitter* submitter_;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
