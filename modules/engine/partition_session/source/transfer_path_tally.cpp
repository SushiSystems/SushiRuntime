/**************************************************************************/
/* transfer_path_tally.cpp                                                */
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
 * @file transfer_path_tally.cpp
 * @brief Implements TransferPathTally.
 * @see modules/engine/partition_session/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/partition_session/transfer_path_tally.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        void TransferPathTally::set_next(std::shared_ptr<TransferObserver> next) noexcept
        {
            next_ = std::move(next);
        }

        void TransferPathTally::copied(const TransferRecord& record)
        {
            {
                const std::lock_guard<std::mutex> hold(mutex_);
                PathTally& tally = tallies_[{record.destination, record.id}];
                if (tally.copies == 0)
                    tally.path = std::string(record.path);
                ++tally.copies;
                tally.bytes += record.bytes;
            }
            if (next_)
                next_->copied(record);
        }

        PathTallies TransferPathTally::tallies() const
        {
            const std::lock_guard<std::mutex> hold(mutex_);
            return tallies_;
        }

        void TransferPathTally::clear()
        {
            const std::lock_guard<std::mutex> hold(mutex_);
            tallies_.clear();
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
