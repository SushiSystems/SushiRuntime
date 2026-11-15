/**************************************************************************/
/* fake_memory_probe.hpp                                                  */
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
 * @file fake_memory_probe.hpp
 * @brief Declares FakeMemoryProbe, a MemoryProbe that reports fixed bytes for tests.
 * @see tests/platform/memory/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <SushiRuntime/memory/memory_probe.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /** @brief Reports the total and free bytes it was built with, and counts its probes. */
        class FakeMemoryProbe final : public MemoryProbe
        {
            public:
                /** @brief Builds the probe with the bytes it reports. */
                FakeMemoryProbe(std::size_t total_bytes, std::size_t free_bytes)
                    : measured_{total_bytes, free_bytes}
                {

                }

                [[nodiscard]] ProbedMemory probe() const override
                {
                    ++probe_count_;
                    return measured_;
                }

                /** @brief Returns how many times probe ran. */
                [[nodiscard]] int probe_count() const noexcept
                {
                    return probe_count_;
                }

            private:
                /// @brief Bytes reported.
                ProbedMemory measured_;

                /// @brief Calls of probe.
                mutable int probe_count_ = 0;
        };
    } // namespace Internal::Memory
} // namespace SushiRuntime
