/**************************************************************************/
/* host_runtime_config.hpp                                                */
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
 * @file host_runtime_config.hpp
 * @brief Declares HostRuntimeConfig, the settings of a runtime that runs on the host with no device.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
 */

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <SushiRuntime/core/clock.hpp>
#include <SushiRuntime/core/seeded_executor.hpp>
#include <SushiRuntime/machine/node_label.hpp>

namespace SushiRuntime
{
    /**
     * @brief Holds every setting Runtime::create_host() builds a host runtime from.
     *
     * A host runtime makes no device, queue or thread: buffers are host memory, and every node
     * runs as a job of the executor on the thread that waits for the run.
     * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
     */
    struct HostRuntimeConfig
    {
        /** @brief Host devices the runtime stands for; at least 1. */
        std::size_t device_count = 1;

        /** @brief Runs every node, in the order its seed draws; null makes one of seed 0. */
        std::shared_ptr<SeededExecutor> executor;

        /** @brief Times every recorded dispatch; null makes a ManualClock at 0. */
        std::shared_ptr<const Clock> clock;

        /** @brief The machine's name in NodeFacts; the host name is never read. */
        std::string node_name = "host";

        /** @brief Labels the machine reports in NodeFacts, in order. */
        std::vector<NodeLabel> labels;
    };
} // namespace SushiRuntime
