/**************************************************************************/
/* peer_path.hpp                                                          */
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
 * @file peer_path.hpp
 * @brief Declares PeerPath, the copy between two devices of one backend that read each other's memory.
 * @see modules/engine/transfer/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <string_view>
#include <vector>

#include <SushiRuntime/transfer/peer_access.hpp>
#include <SushiRuntime/transfer/space_queues.hpp>
#include <SushiRuntime/transfer/transfer_observer.hpp>
#include <SushiRuntime/transfer/transfer_path.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Copies with one peer copy on the destination's queue, between spaces of two contexts.
         *
         * It connects two spaces when the source does not list the destination as a direct target
         * and PeerAccess grants the destination's device the source's memory. Where the backend has
         * no peer access, PeerAccess refuses and the path connects nothing.
         */
        class PeerPath final : public TransferPath
        {
            public:
                /**
                 * @param queues Names the destination's queue; must outlive the path.
                 * @param access Grants peer access; must outlive the path.
                 * @param observer Hears each copy; null for none; must outlive the path.
                 */
                PeerPath(const SpaceQueues& queues, PeerAccess& access,
                         TransferObserver* observer = nullptr) noexcept;

                [[nodiscard]] std::string_view name() const noexcept override;

                [[nodiscard]] bool connects(const Memory::MemorySpace& source,
                                            const Memory::MemorySpace& destination) const override;

                /** @throws std::runtime_error when the destination space has no queue. */
                [[nodiscard]] sycl::event copy(const TransferRequest& request,
                                               const std::vector<sycl::event>& dependencies) override;

            private:
                /// @brief Names the destination's queue.
                const SpaceQueues* queues_;

                /// @brief Grants peer access between two spaces.
                PeerAccess* access_;

                /// @brief Hears each copy; null for none.
                TransferObserver* observer_;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
