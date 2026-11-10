/**************************************************************************/
/* host_context.hpp                                                       */
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
 * @file host_context.hpp
 * @brief Declares make_host_context(), which builds a host RuntimeContext for an engine test.
 * @see tests/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <memory>

#include <SushiRuntime/core/clock.hpp>
#include <SushiRuntime/core/executor.hpp>
#include <SushiRuntime/core/seeded_executor.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/machine/device_kind.hpp>

#include "support/host_runtime.hpp"

namespace TestSupport
{
    /**
     * @brief Builds a host context of some devices on an executor and a clock.
     * @param executor Null makes a SeededExecutor of K_HOST_RUNTIME_SEED.
     * @param clock Null makes a ManualClock at 0.
     * @param device_kind The kind every host device reports; a GPU makes a context that answers as one.
     */
    [[nodiscard]] inline std::shared_ptr<SushiRuntime::Internal::Execution::RuntimeContext>
    make_host_context(std::shared_ptr<SushiRuntime::Executor> executor = nullptr,
                      std::shared_ptr<const SushiRuntime::Clock> clock = nullptr,
                      std::size_t device_count = 1,
                      SushiRuntime::DeviceKind device_kind = SushiRuntime::DeviceKind::CPU)
    {
        if (executor == nullptr)
            executor = std::make_shared<SushiRuntime::SeededExecutor>(K_HOST_RUNTIME_SEED);
        if (clock == nullptr)
            clock = std::make_shared<SushiRuntime::ManualClock>();
        return SushiRuntime::Internal::Execution::RuntimeContext::create_host(
                   device_count, std::move(executor), std::move(clock), nullptr, nullptr,
                   SushiRuntime::Internal::Profile::process_collector(), device_kind)
            .value();
    }
} // namespace TestSupport
