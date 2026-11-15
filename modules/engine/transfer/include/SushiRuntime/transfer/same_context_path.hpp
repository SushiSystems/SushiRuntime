/**************************************************************************/
/* same_context_path.hpp                                                  */
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
 * @file same_context_path.hpp
 * @brief Declares SameContextPath, the copy between two devices of one SYCL context.
 * @see modules/engine/transfer/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <string_view>
#include <vector>

#include <SushiRuntime/transfer/space_queues.hpp>
#include <SushiRuntime/transfer/transfer_observer.hpp>
#include <SushiRuntime/transfer/transfer_path.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Copies with one memcpy on the destination's queue, between spaces of one context.
         *
         * Two spaces are in one context when the source lists the destination among its direct
         * targets.
         */
        class SameContextPath final : public TransferPath
        {
            public:
                /**
                 * @param queues Names the destination's queue; must outlive the path.
                 * @param observer Hears each copy; null for none; must outlive the path.
                 */
                explicit SameContextPath(const SpaceQueues& queues, TransferObserver* observer = nullptr) noexcept;

                /// @brief Name of the path, for logs and the profile file.
                static constexpr std::string_view K_NAME = "same_context";

                /// @brief Identity of the path; every comparison and price keys on it.
                static constexpr TransferPathID K_ID = TransferPathID::named(K_NAME);

                [[nodiscard]] std::string_view name() const noexcept override;

                [[nodiscard]] TransferPathID id() const noexcept override;

                [[nodiscard]] bool connects(const Memory::MemorySpace& source,
                                            const Memory::MemorySpace& destination) const override;

                /** @return An error when the destination space has no queue or the device call fails. */
                [[nodiscard]] Result<sycl::event> copy(const TransferRequest& request,
                                               const std::vector<sycl::event>& dependencies) override;

            private:
                /// @brief Names the destination's queue.
                const SpaceQueues* queues_;

                /// @brief Hears each copy; null for none.
                TransferObserver* observer_;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
