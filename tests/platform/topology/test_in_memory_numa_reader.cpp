/**************************************************************************/
/* test_in_memory_numa_reader.cpp                                         */
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
 * @file test_in_memory_numa_reader.cpp
 * @brief Tests the in-memory NUMAReader against the NUMAReader contract.
 * @see tests/platform/topology/README.md
 * @author Mustafa Garip
 */

#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/topology/in_memory_numa_reader.hpp>
#include <vector>

#include "support/numa_reader_contract.hpp"

namespace
{
    /** @brief Makes the reader the contract runs on: the CPU on node 1 and one listed GPU. */
    struct InMemoryReaderMaker
    {
        /** @brief Returns the adapter under test. */
        static std::unique_ptr<SushiRuntime::Internal::Topology::NUMAReader> make()
        {
            return std::make_unique<SushiRuntime::Internal::Topology::InMemoryNUMAReader>(
                1, std::vector<SushiRuntime::Internal::Topology::ListedGPU>{{"RTX", "0000:01:00.0"}});
        }
    };
} // namespace

namespace TestSupport
{
    /** @brief Runs the NUMAReader contract on the in-memory reader. */
    INSTANTIATE_TYPED_TEST_SUITE_P(Small_InMemoryNUMAReader, NUMAReaderContract, ::InMemoryReaderMaker, );
} // namespace TestSupport
