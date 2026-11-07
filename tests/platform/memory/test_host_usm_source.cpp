/**************************************************************************/
/* test_host_usm_source.cpp                                               */
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
 * @file test_host_usm_source.cpp
 * @brief Tests the host USMSource against the USMSource contract and its own host promises.
 * @see tests/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <memory>

#include <gtest/gtest.h>

#include <SushiRuntime/memory/host_usm_source.hpp>
#include <SushiRuntime/memory/usm_request.hpp>

#include "support/usm_source_contract.hpp"

using SushiRuntime::Internal::Memory::make_host_usm_source;
using SushiRuntime::Internal::Memory::USMKind;
using SushiRuntime::Internal::Memory::USMRequest;
using SushiRuntime::Internal::Memory::USMSource;

namespace
{
    /** @brief Makes the source the contract runs on: a host source of one device. */
    struct HostSourceMaker
    {
        /** @brief Returns a host source of one device. */
        static std::unique_ptr<USMSource> make() { return make_host_usm_source(); }
    };
} // namespace

namespace TestSupport
{
    /** @brief Runs the USMSource contract on the host source. */
    INSTANTIATE_TYPED_TEST_SUITE_P(Small_HostUSMSource, USMSourceContract, ::HostSourceMaker, );
} // namespace TestSupport

/** @brief Tests that an alignment that is not a power of two is refused with null. */
TEST(Small_HostUSMSource, RefusesAnAlignmentThatIsNotAPowerOfTwo)
{
    const std::unique_ptr<USMSource> source = make_host_usm_source();
    USMRequest request;
    request.kind = USMKind::Device;
    request.bytes = 16;
    request.alignment = 48;

    EXPECT_EQ(source->allocate(request), nullptr);
}

/** @brief Tests that every pair of devices of a host source shares one context. */
TEST(Small_HostUSMSource, TwoDevicesShareOneContext)
{
    const std::unique_ptr<USMSource> source = make_host_usm_source(2);

    EXPECT_TRUE(source->shares_context(0, 1));
}

/** @brief Tests that a source counts the devices it was made with. */
TEST(Small_HostUSMSource, CountsTheDevicesItWasMadeWith)
{
    const std::unique_ptr<USMSource> source = make_host_usm_source(3);

    EXPECT_EQ(source->device_count(), 3u);
}
