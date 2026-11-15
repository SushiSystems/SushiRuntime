/**************************************************************************/
/* test_residency_reader.cpp                                              */
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
 * @file test_residency_reader.cpp
 * @brief Tests ResidencyReader::home_of(): the home, residency, memory kind and layout it reads from inspect().
 * @see tests/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <optional>

#include <gtest/gtest.h>

#include <SushiRuntime/handle/memory_space_id.hpp>
#include <SushiRuntime/memory/residency_directory.hpp>
#include <SushiRuntime/memory/residency_reader.hpp>
#include <SushiRuntime/memory/usm_request.hpp>

using namespace SushiRuntime;
using Internal::Memory::ElementLayout;
using Internal::Memory::ResidencyDirectory;
using Internal::Memory::USMKind;

namespace
{
    /** @brief The space every buffer of these tests is made on. */
    constexpr std::uint32_t K_HOME_SPACE = 2;

    /** @brief Holds a directory and a buffer of 64 bytes of 8-byte elements made on K_HOME_SPACE. */
    class Small_ResidencyReaderHome : public ::testing::Test
    {
        protected:
            /** @brief Mints and describes the buffer. */
            void SetUp() override
            {
                const MintOutcome minted = directory_.mint(HandleKind::Buffer, HandleFlags::None);
                ASSERT_EQ(minted.status, HandleStatus::Ok);
                handle_ = minted.handle;
                ASSERT_EQ(directory_.describe(handle_, 64, ElementLayout{8, 8}, MemorySpaceID(K_HOME_SPACE)),
                          HandleStatus::Ok);
            }

            /** @brief Records an instance on the home space with @p kind. */
            void instance_of_kind(std::optional<USMKind> kind)
            {
                ASSERT_EQ(directory_.set_instance(handle_, MemorySpaceID(K_HOME_SPACE), &storage_, kind),
                          HandleStatus::Ok);
            }

            ResidencyDirectory directory_{0}; /**< The directory read. */
            GlobalHandle handle_;             /**< The buffer. */
            std::uint64_t storage_ = 0;       /**< Stands for the home instance. */
    };

    /** @brief Tests that the home carries the space and the layout the buffer was described with. */
    TEST_F(Small_ResidencyReaderHome, TheHomeCarriesTheSpaceAndTheLayoutOfTheBuffer)
    {
        instance_of_kind(USMKind::Device);

        const HandleHome home = directory_.home_of(handle_);

        EXPECT_EQ(home.status, HandleStatus::Ok);
        EXPECT_EQ(home.home, MemorySpaceID(K_HOME_SPACE));
        EXPECT_EQ(home.layout, (ElementLayout{8, 8}));
    }

    /** @brief Tests that a home that holds an instance is resident and one that does not is not. */
    TEST_F(Small_ResidencyReaderHome, TheHomeIsResidentOnlyWhileItHoldsAnInstance)
    {
        const bool before = directory_.home_of(handle_).resident;
        instance_of_kind(USMKind::Device);
        const bool during = directory_.home_of(handle_).resident;
        ASSERT_EQ(directory_.drop_instance(handle_, MemorySpaceID(K_HOME_SPACE)), HandleStatus::Ok);
        const bool after = directory_.home_of(handle_).resident;

        EXPECT_FALSE(before);
        EXPECT_TRUE(during);
        EXPECT_FALSE(after);
    }

    /** @brief Tests that host and shared USM read as host accessible and device USM as device only. */
    TEST_F(Small_ResidencyReaderHome, HostAndSharedMemoryAreHostAccessibleAndDeviceMemoryIsNot)
    {
        instance_of_kind(USMKind::Host);
        const HomeMemory host = directory_.home_of(handle_).memory;
        instance_of_kind(USMKind::Shared);
        const HomeMemory shared = directory_.home_of(handle_).memory;
        instance_of_kind(USMKind::Device);
        const HomeMemory device = directory_.home_of(handle_).memory;

        EXPECT_EQ(host, HomeMemory::HostAccessible);
        EXPECT_EQ(shared, HomeMemory::HostAccessible);
        EXPECT_EQ(device, HomeMemory::DeviceOnly);
    }

    /** @brief Tests that an instance with no recorded kind reads as unknown memory. */
    TEST_F(Small_ResidencyReaderHome, AnInstanceWithNoKindReadsAsUnknownMemory)
    {
        instance_of_kind(std::nullopt);

        EXPECT_EQ(directory_.home_of(handle_).memory, HomeMemory::Unknown);
    }

    /** @brief Tests that a retired handle has no home and says why. */
    TEST_F(Small_ResidencyReaderHome, ARetiredHandleHasNoHomeAndSaysWhy)
    {
        instance_of_kind(USMKind::Host);
        ASSERT_EQ(directory_.retire(handle_), HandleStatus::Ok);

        const HandleHome home = directory_.home_of(handle_);

        EXPECT_EQ(home.status, HandleStatus::StaleHandle);
        EXPECT_FALSE(home.resident);
    }
} // namespace
