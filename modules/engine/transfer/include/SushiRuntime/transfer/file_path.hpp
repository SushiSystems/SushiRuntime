/**************************************************************************/
/* file_path.hpp                                                          */
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
 * @file file_path.hpp
 * @brief Declares FilePath, the copy from a file space to a host or device space through the staging ring.
 * @see modules/engine/transfer/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <string_view>
#include <vector>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/memory/file_memory_space.hpp>
#include <SushiRuntime/transfer/space_queues.hpp>
#include <SushiRuntime/transfer/staging_rings.hpp>
#include <SushiRuntime/transfer/transfer_observer.hpp>
#include <SushiRuntime/transfer/transfer_path.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Copies bytes out of a file space into any space that is not a file.
         *
         * A copy to a device reads the file one slot of the ring of the device's context at a
         * time, in file order, and copies each slot on the device's queue, so the host memory it
         * holds is the ring. A copy to the host reads the file straight into the destination.
         */
        class SUSHIRUNTIME_API FilePath final : public TransferPath
        {
            public:
                /**
                 * @param queues Names the queue of a space; a space with none is read as host memory; must outlive the path.
                 * @param rings Holds the staging ring of each context; must outlive the path.
                 * @param observer Hears each copy; null for none; must outlive the path.
                 */
                FilePath(const SpaceQueues& queues, const StagingRings& rings, TransferObserver* observer = nullptr) noexcept;

                /**
                 * @brief Makes the bytes of space readable by copy().
                 *
                 * Called between runs, never while a copy runs.
                 * @param space Must outlive the path.
                 */
                void add(const Memory::FileMemorySpace& space);

                /**
                 * @brief Stops reading the file space with id; an id never added is left alone.
                 *
                 * Called between runs, never while a copy runs.
                 */
                void remove(MemorySpaceID id) noexcept;

                /// @brief Name of the path, for logs and the profile file.
                static constexpr std::string_view K_NAME = "file";

                /// @brief Identity of the path; every comparison and price keys on it.
                static constexpr TransferPathID K_ID = TransferPathID::named(K_NAME);

                [[nodiscard]] std::string_view name() const noexcept override;

                [[nodiscard]] TransferPathID id() const noexcept override;

                /** @brief Returns whether source is a file space added here and destination is not a file. */
                [[nodiscard]] bool connects(const Memory::MemorySpace& source,
                                            const Memory::MemorySpace& destination) const override;

                /**
                 * @return An error when the source space was not added, the file cannot be opened or
                 *         ends before the range does, the ring has no slots or a device call fails.
                 */
                [[nodiscard]] Result<sycl::event> copy(const TransferRequest& request,
                                               const std::vector<sycl::event>& dependencies) override;

            private:
                /** @brief Returns the added space with id, or null. */
                [[nodiscard]] const Memory::FileMemorySpace* space_with(MemorySpaceID id) const noexcept;

                /// @brief Names the queue of a space.
                const SpaceQueues* queues_;

                /// @brief Holds the staging ring of each context.
                const StagingRings* rings_;

                /// @brief Hears each copy; null for none.
                TransferObserver* observer_;

                /// @brief The file spaces this path reads, sorted by id.
                std::vector<const Memory::FileMemorySpace*> spaces_;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
