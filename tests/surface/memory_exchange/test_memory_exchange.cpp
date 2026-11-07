/**************************************************************************/
/* test_memory_exchange.cpp                                               */
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
 * @file test_memory_exchange.cpp
 * @brief Tests the memory exchange: capabilities, a host export, and an import that names no mechanism.
 * @see tests/surface/memory_exchange/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/context_access/context_access.hpp>
#include <SushiRuntime/core/allocation.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/interop/external_handle_kind_set.hpp>
#include <SushiRuntime/interop/external_memory.hpp>
#include <SushiRuntime/interop/interop_capabilities.hpp>

#include <SushiRuntime/memory_exchange/capability_cache.hpp>
#include <SushiRuntime/memory_exchange/memory_exchange.hpp>
#include <SushiRuntime/memory_exchange/scripted_capability_probe.hpp>

#include "support/host_runtime.hpp"
#include "support/runtime_environment.hpp"

using namespace SushiRuntime;
using Internal::MemoryExchange::CapabilityCache;
using Internal::MemoryExchange::ScriptedCapabilityProbe;

/**
 * @brief Runs the memory exchange tests on the program's one runtime.
 */
class Medium_MemoryExchange : public TestSupport::RuntimeTest
{
    protected:
        /** @brief Returns a share of the context of the program's runtime. */
        [[nodiscard]] static std::shared_ptr<Internal::Execution::RuntimeContext> shared_context()
        {
            return Internal::ContextAccess::shared(runtime());
        }
};

/** @brief Runs the host export test on a host runtime, which makes no device. */
class Small_MemoryExchange : public TestSupport::HostRuntimeTest
{
};

/** @brief Tests that a host export gives an allocation of the size asked. */
TEST_F(Small_MemoryExchange, AHostExportGivesTheSizeAsked)
{
    const auto context = Internal::ContextAccess::shared(runtime());

    const Result<Detail::Allocation> exported =
        Internal::MemoryExchange::export_host_memory(context, 4096, 0, 4096);

    ASSERT_TRUE(exported.has_value()) << exported.error().message;
    EXPECT_EQ(exported.value().bytes(), 4096u);
}

/**
 * @brief Runs the import tests on the program's one runtime. A device that is not a GPU refuses
 *        every import before it reads the descriptor, so a program without a GPU skips them.
 */
class Medium_MemoryExchangeImport : public Medium_MemoryExchange
{
    protected:
        /** @brief Skips the test when device 0 is not a GPU. */
        void SetUp() override
        {
            if (!shared_context()->devices().is_gpu(0))
                GTEST_SKIP() << "device 0 is not a GPU, so it imports nothing";
        }

        /** @brief Returns the capabilities of a GPU that imports an opaque file descriptor and nothing else. */
        [[nodiscard]] static InteropCapabilities fd_importer()
        {
            InteropCapabilities capabilities;
            capabilities.device_import = true;
            capabilities.importable_kinds.insert(ExternalHandleKind::OpaqueFd);
            return capabilities;
        }
};

/** @brief Tests that device 0 reports the capabilities its probe answered. */
TEST_F(Medium_MemoryExchange, CapabilitiesOfDeviceZero)
{
    ScriptedCapabilityProbe probe;
    InteropCapabilities answer;
    answer.host_export = true;
    probe.answer_next(answer);
    CapabilityCache cache(std::ref(probe));
    const auto context = shared_context();

    const Result<InteropCapabilities> capabilities =
        Internal::MemoryExchange::query_capabilities(*context, cache, 0);

    ASSERT_TRUE(capabilities.has_value());
    EXPECT_TRUE(capabilities.value().host_export);
}

/** @brief Tests that an import runs the descriptor checks: one naming no handle kind is refused with InvalidAccess. */
TEST_F(Medium_MemoryExchangeImport, AnImportWithNoKindIsRefused)
{
    ScriptedCapabilityProbe probe;
    probe.answer_next(fd_importer());
    CapabilityCache cache(std::ref(probe));
    const auto context = shared_context();
    ExternalMemoryDescriptor descriptor;
    descriptor.allocation_bytes = 4096;

    const Result<Detail::Allocation> imported =
        Internal::MemoryExchange::import_device_memory(context, cache, descriptor, 4096, 0);

    ASSERT_FALSE(imported.has_value());
    EXPECT_EQ(imported.error().code, ErrorCode::InvalidAccess);
    EXPECT_NE(imported.error().message.find("Unspecified"), std::string::npos)
        << imported.error().message;
}
