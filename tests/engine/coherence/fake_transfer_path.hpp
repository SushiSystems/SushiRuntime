/**************************************************************************/
/* fake_transfer_path.hpp                                                 */
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
 * @file fake_transfer_path.hpp
 * @brief Declares FakeTransferPath and FixedMemoryProbe, the test doubles of the coherence tests.
 * @see tests/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstring>
#include <string_view>
#include <vector>

#include <SushiRuntime/coherence/transfer_path.hpp>
#include <SushiRuntime/memory/memory_probe.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Connects every pair of spaces, copies with memcpy on the host and counts its copies. */
        class FakeTransferPath final : public TransferPath
        {
            public:
                [[nodiscard]] std::string_view name() const noexcept override
                {
                    return "fake";
                }

                [[nodiscard]] bool connects(const Memory::MemorySpace&,
                                            const Memory::MemorySpace&) const override
                {
                    return true;
                }

                [[nodiscard]] sycl::event copy(const TransferRequest& request,
                                               const std::vector<sycl::event>&) override
                {
                    std::memcpy(request.to, request.from, request.bytes);
                    ++copies_;
                    return sycl::event{};
                }

                /** @brief Returns how many copies ran. */
                [[nodiscard]] int copies() const noexcept
                {
                    return copies_;
                }

            private:
                /// @brief Copies run.
                int copies_ = 0;
        };

        /** @brief Connects no pair of spaces. */
        class ClosedTransferPath final : public TransferPath
        {
            public:
                [[nodiscard]] std::string_view name() const noexcept override
                {
                    return "closed";
                }

                [[nodiscard]] bool connects(const Memory::MemorySpace&,
                                            const Memory::MemorySpace&) const override
                {
                    return false;
                }

                [[nodiscard]] sycl::event copy(const TransferRequest&,
                                               const std::vector<sycl::event>&) override
                {
                    return sycl::event{};
                }
        };

        /** @brief Reports one gigabyte, all of it free. */
        class FixedMemoryProbe final : public Memory::MemoryProbe
        {
            public:
                [[nodiscard]] Memory::ProbedMemory probe() const override
                {
                    return Memory::ProbedMemory{std::size_t{1} << 30, std::size_t{1} << 30};
                }
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
