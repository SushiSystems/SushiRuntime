/**************************************************************************/
/* transfer_observer_slot.cpp                                             */
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
 * @file transfer_observer_slot.cpp
 * @brief Implements TransferObserverSlot.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/coherence/transfer_observer_slot.hpp>

#include <utility>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        void TransferObserverSlot::install(std::shared_ptr<TransferObserver> observer) noexcept
        {
            observer_ = std::move(observer);
        }

        TransferObserver* TransferObserverSlot::installed() const noexcept
        {
            return observer_.get();
        }

        void TransferObserverSlot::copied(const TransferRecord& record)
        {
            if (observer_)
                observer_->copied(record);
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
