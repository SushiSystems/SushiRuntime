/**************************************************************************/
/* transfer_path_tally.hpp                                                */
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
 * @file transfer_path_tally.hpp
 * @brief Declares TransferPathTally, the observer that counts copies per destination and path.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include <SushiRuntime/coherence/transfer_observer.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Holds the copies one destination received over one path. */
        struct PathTally
        {
            std::uint64_t copies = 0; /**< Copies counted. */
            std::uint64_t bytes = 0;  /**< Bytes those copies moved. */
        };

        /** @brief Holds the tallies by destination space, then path name. */
        using PathTallies = std::map<std::pair<MemorySpaceID, std::string>, PathTally>;

        /**
         * @brief Counts each copy by its destination space and path, then hands it to the next observer.
         *
         * Paths call copied() from several workers, so the count takes a lock per copy; a copy is
         * one transfer, never a kernel dispatch. set_next() runs between runs only.
         */
        class TransferPathTally final : public TransferObserver
        {
            public:
                /**
                 * @brief Installs the observer each copy goes to after it is counted; null for none.
                 * @pre No copy is running.
                 */
                void set_next(std::shared_ptr<TransferObserver> next) noexcept;

                /** @brief Counts record and forwards it. */
                void copied(const TransferRecord& record) override;

                /** @brief Returns the tallies counted since the last clear(). */
                [[nodiscard]] PathTallies tallies() const;

                /** @brief Forgets every tally. */
                void clear();

            private:
                /// @brief Guards tallies_.
                mutable std::mutex mutex_;

                /// @brief Copies by destination space and path name.
                PathTallies tallies_;

                /// @brief Observer each copy is forwarded to; null for none.
                std::shared_ptr<TransferObserver> next_;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
