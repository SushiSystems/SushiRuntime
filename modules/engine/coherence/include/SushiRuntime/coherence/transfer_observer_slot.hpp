/**************************************************************************/
/* transfer_observer_slot.hpp                                             */
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
 * @file transfer_observer_slot.hpp
 * @brief Declares TransferObserverSlot, the observer a runtime's paths hold, which forwards to the one a caller installed.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <memory>

#include <SushiRuntime/coherence/transfer_observer.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Forwards each copy to the observer installed in it, or drops it when none is.
         *
         * The paths take their observer when they are built; the slot lets a caller install one
         * later. install() runs between runs only, so copied() reads the observer without a lock.
         */
        class TransferObserverSlot final : public TransferObserver
        {
            public:
                /**
                 * @brief Installs observer; null removes the one installed.
                 * @pre No copy is running.
                 */
                void install(std::shared_ptr<TransferObserver> observer) noexcept;

                /** @brief Returns the installed observer, or null for none. */
                [[nodiscard]] TransferObserver* installed() const noexcept;

                /** @brief Hands record to the installed observer, if any. */
                void copied(const TransferRecord& record) override;

            private:
                /// @brief The observer copies are forwarded to; co-owned while installed.
                std::shared_ptr<TransferObserver> observer_;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
