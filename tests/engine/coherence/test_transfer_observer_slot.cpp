/**************************************************************************/
/* test_transfer_observer_slot.cpp                                        */
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
 * @file test_transfer_observer_slot.cpp
 * @brief Tests that TransferObserverSlot forwards copies to the observer installed in it.
 * @see tests/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/coherence/transfer_observer_slot.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    /** @brief Keeps the byte count of each copy it hears of. */
    class CountingObserver final : public TransferObserver
    {
        public:
            void copied(const TransferRecord& record) override
            {
                bytes.push_back(record.bytes);
            }

            std::vector<std::size_t> bytes; /**< Bytes of each copy heard, in order. */
    };

    /** @brief Returns the record of a copy of bytes. */
    TransferRecord record_of(std::size_t bytes)
    {
        TransferRecord record;
        record.path = "fake";
        record.bytes = bytes;
        return record;
    }

    TEST(Small_TransferObserverSlot, AnEmptySlotDropsEveryCopy)
    {
        TransferObserverSlot slot;
        slot.copied(record_of(8));
        EXPECT_EQ(slot.installed(), nullptr);
    }

    TEST(Small_TransferObserverSlot, AnInstalledObserverHearsEachCopyInOrder)
    {
        TransferObserverSlot slot;
        const auto observer = std::make_shared<CountingObserver>();
        slot.install(observer);
        slot.copied(record_of(8));
        slot.copied(record_of(16));
        EXPECT_EQ(observer->bytes, (std::vector<std::size_t>{8, 16}));
    }

    TEST(Small_TransferObserverSlot, InstallingNullStopsForwarding)
    {
        TransferObserverSlot slot;
        const auto observer = std::make_shared<CountingObserver>();
        slot.install(observer);
        slot.install(nullptr);
        slot.copied(record_of(8));
        EXPECT_TRUE(observer->bytes.empty());
    }
}
