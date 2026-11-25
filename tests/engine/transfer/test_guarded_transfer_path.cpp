/**************************************************************************/
/* test_guarded_transfer_path.cpp                                         */
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
 * @file test_guarded_transfer_path.cpp
 * @brief Tests that a guarded transfer path copies guarded pages and leaves its gate closed after each copy.
 * @see tests/engine/transfer/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstring>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/memory/device_access_scope.hpp>
#include <SushiRuntime/memory/guarded_device_pages.hpp>
#include <SushiRuntime/transfer/guarded_transfer_path.hpp>

#include "support/fake_transfer_path.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;
using SushiRuntime::Internal::Memory::DeviceAccessScope;
using SushiRuntime::Internal::Memory::GuardedDevicePages;

namespace
{
    /** @brief The bytes each case copies. */
    constexpr std::size_t K_BYTES = 64;

    /** @brief Returns a request that copies K_BYTES from from to to. */
    TransferRequest request_of(const void* from, void* to)
    {
        TransferRequest request;
        request.from = from;
        request.to = to;
        request.bytes = K_BYTES;
        return request;
    }
} // namespace

/** @brief Tests that a copy through the guarded path reads guarded pages while no caller opened them. */
TEST(Small_GuardedTransferPath, ACopyReadsGuardedPagesTheCallerLeftClosed)
{
    GuardedDevicePages pages;
    void* const source = pages.allocate(K_BYTES, 0);
    ASSERT_NE(source, nullptr);
    {
        const DeviceAccessScope window(&pages);
        std::memset(source, 0x5A, K_BYTES);
    }
    std::unique_ptr<TransferPath> path = make_guarded_transfer_path(std::make_unique<FakeTransferPath>(), pages);
    std::vector<unsigned char> host(K_BYTES, 0);

    const Result<sycl::event> copied = path->copy(request_of(source, host.data()), {});

    ASSERT_TRUE(copied.has_value());
    EXPECT_EQ(host, std::vector<unsigned char>(K_BYTES, 0x5A));
    EXPECT_FALSE(pages.is_open());
    pages.release(source);
}

/** @brief Tests that a copy the wrapped path refuses returns its error and leaves the gate closed. */
TEST(Small_GuardedTransferPath, ARefusedCopyReturnsItsErrorAndClosesTheGate)
{
    GuardedDevicePages pages;
    auto inner = std::make_unique<FakeTransferPath>();
    inner->fail_with(Error{ErrorCode::KernelFailed, "refused"});
    std::unique_ptr<TransferPath> path = make_guarded_transfer_path(std::move(inner), pages);
    std::vector<unsigned char> from(K_BYTES, 1);
    std::vector<unsigned char> to(K_BYTES, 0);

    const Result<sycl::event> copied = path->copy(request_of(from.data(), to.data()), {});

    ASSERT_FALSE(copied.has_value());
    EXPECT_EQ(copied.error().code, ErrorCode::KernelFailed);
    EXPECT_FALSE(pages.is_open());
}
