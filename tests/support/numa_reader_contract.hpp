/**************************************************************************/
/* numa_reader_contract.hpp                                               */
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
 * @file numa_reader_contract.hpp
 * @brief Declares NUMAReaderContract, the typed suite every NUMAReader adapter passes.
 * @see tests/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <memory>
#include <optional>

#include <gtest/gtest.h>

#include <SushiRuntime/topology/numa_reader.hpp>

namespace TestSupport
{
    /**
     * @brief Fixture of the reader contract; Maker::make() returns the reader under test.
     * @tparam Maker A type with a static make() returning std::unique_ptr<NUMAReader>.
     */
    template <class Maker>
    class NUMAReaderContract : public ::testing::Test
    {
        protected:
            /** @brief Makes the reader each case runs on. */
            void SetUp() override { reader_ = Maker::make(); }

            /** @brief Returns the reader under test. */
            [[nodiscard]] const SushiRuntime::Internal::Topology::NUMAReader& reader() const { return *reader_; }

        private:
            /** @brief The reader under test, one per case. */
            std::unique_ptr<SushiRuntime::Internal::Topology::NUMAReader> reader_;
    };

    TYPED_TEST_SUITE_P(NUMAReaderContract);

    /** @brief Tests that a loaded reader places the CPU on a node that is not negative. */
    TYPED_TEST_P(NUMAReaderContract, ALoadedReaderPlacesTheCPUOnANode)
    {
        ASSERT_TRUE(this->reader().loaded());
        const std::optional<int> node = this->reader().cpu_numa_node();

        ASSERT_TRUE(node.has_value());
        EXPECT_GE(*node, 0);
    }

    /** @brief Tests that a name no GPU holds has no PCI address. */
    TYPED_TEST_P(NUMAReaderContract, ANameNoGPUHoldsHasNoAddress)
    {
        EXPECT_TRUE(this->reader().gpu_pci_address("no such device \x01").empty());
    }

    REGISTER_TYPED_TEST_SUITE_P(NUMAReaderContract, ALoadedReaderPlacesTheCPUOnANode,
                                ANameNoGPUHoldsHasNoAddress);
} // namespace TestSupport
