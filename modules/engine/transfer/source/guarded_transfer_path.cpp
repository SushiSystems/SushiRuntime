/**************************************************************************/
/* guarded_transfer_path.cpp                                              */
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
 * @file guarded_transfer_path.cpp
 * @brief Defines GuardedTransferPath, which runs each copy of a path with the device gate open.
 * @author Mustafa Garip
 * @see modules/engine/transfer/README.md
 */

#include <SushiRuntime/transfer/guarded_transfer_path.hpp>

#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include <SushiRuntime/core/assert.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/memory/device_access_scope.hpp>
#include <SushiRuntime/sycl/error_translation.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        namespace
        {
            /** @brief Forwards every call to the path it wraps and runs each copy inside the gate. */
            class GuardedTransferPath final : public TransferPath
            {
                public:
                    /** @brief Wraps a path that is not null. */
                    GuardedTransferPath(std::unique_ptr<TransferPath> inner, Memory::DeviceAccessGate& gate)
                        : inner_(std::move(inner)), gate_(gate)
                    {
                        SR_VERIFY(inner_ != nullptr, "make_guarded_transfer_path: pass a path, not null.");
                    }

                    [[nodiscard]] std::string_view name() const noexcept override
                    {
                        return inner_->name();
                    }

                    [[nodiscard]] TransferPathID id() const noexcept override
                    {
                        return inner_->id();
                    }

                    [[nodiscard]] bool connects(const Memory::MemorySpace& source,
                                                const Memory::MemorySpace& destination) const override
                    {
                        return inner_->connects(source, destination);
                    }

                    /** @brief Copies with the gate open and returns once the copy has ended. */
                    [[nodiscard]] Result<sycl::event> copy(const TransferRequest& request,
                                                           const std::vector<sycl::event>& dependencies) override
                    {
                        const Memory::DeviceAccessScope window(&gate_);
                        Result<sycl::event> copied = inner_->copy(request, dependencies);
                        if (!copied)
                            return copied;
                        // The gate closes when this returns, so the copy ends inside it.
                        Result<void> waited = Internal::SYCL::translate_exceptions([&copied]() { copied.value().wait(); });
                        if (!waited)
                            return make_error(std::move(waited.error()));
                        return copied;
                    }

                private:
                    /** @brief The path every call goes to. */
                    std::unique_ptr<TransferPath> inner_;

                    /** @brief The gate each copy opens. */
                    Memory::DeviceAccessGate& gate_;
            };
        } // namespace

        std::unique_ptr<TransferPath> make_guarded_transfer_path(std::unique_ptr<TransferPath> inner,
                                                                 Memory::DeviceAccessGate& gate)
        {
            return std::make_unique<GuardedTransferPath>(std::move(inner), gate);
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
