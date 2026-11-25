/**************************************************************************/
/* guarded_usm_source.cpp                                                 */
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
 * @file guarded_usm_source.cpp
 * @brief Defines GuardedUSMSource, which serves Device requests from guarded pages, and its factory.
 * @author Mustafa Garip
 * @see modules/platform/memory/README.md
 */

#include <SushiRuntime/memory/guarded_usm_source.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <SushiRuntime/core/assert.hpp>
#include <SushiRuntime/memory/device_access_scope.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        namespace
        {
            /**
             * @brief Serves Device requests from guarded pages and forwards everything else to the
             *        source it wraps.
             */
            class GuardedUSMSource final : public USMSource
            {
                public:
                    /** @brief Wraps a source; both arguments are not null. */
                    GuardedUSMSource(std::unique_ptr<USMSource> inner, std::shared_ptr<GuardedDevicePages> pages);

                    [[nodiscard]] std::size_t device_count() const noexcept override;
                    [[nodiscard]] void* allocate(const USMRequest& request) override;
                    void free(void* address, std::size_t device_index) override;
                    [[nodiscard]] bool shares_context(std::size_t first,
                                                      std::size_t second) const override;
                    void copy(void* destination, const void* source, std::size_t bytes,
                              std::size_t device_index) override;
                    [[nodiscard]] std::string device_name(std::size_t index) const override;
                    [[nodiscard]] std::optional<USMKind> kind_of(const void* address,
                                                                 std::size_t device_index) const override;

                private:
                    /** @brief Fails a device index the wrapped source does not hold. */
                    void require_device(std::size_t index) const;

                    /** @brief The source every request but a Device one goes to. */
                    std::unique_ptr<USMSource> inner_;

                    /** @brief The pages Device requests come from. */
                    std::shared_ptr<GuardedDevicePages> pages_;
            };
        } // namespace

        GuardedUSMSource::GuardedUSMSource(std::unique_ptr<USMSource> inner,
                                           std::shared_ptr<GuardedDevicePages> pages)
            : inner_(std::move(inner)), pages_(std::move(pages))
        {
        }

        std::size_t GuardedUSMSource::device_count() const noexcept
        {
            return inner_->device_count();
        }

        void* GuardedUSMSource::allocate(const USMRequest& request)
        {
            if (request.kind != USMKind::Device)
                return inner_->allocate(request);
            require_device(request.device_index);
            return pages_->allocate(request.bytes, request.alignment);
        }

        void GuardedUSMSource::free(void* address, std::size_t device_index)
        {
            if (address != nullptr && pages_->holds(address))
            {
                require_device(device_index);
                pages_->release(address);
                return;
            }
            inner_->free(address, device_index);
        }

        bool GuardedUSMSource::shares_context(std::size_t first, std::size_t second) const
        {
            return inner_->shares_context(first, second);
        }

        void GuardedUSMSource::copy(void* destination, const void* source, std::size_t bytes,
                                    std::size_t device_index)
        {
            const DeviceAccessScope open(pages_.get());
            inner_->copy(destination, source, bytes, device_index);
        }

        std::string GuardedUSMSource::device_name(std::size_t index) const
        {
            return inner_->device_name(index);
        }

        std::optional<USMKind> GuardedUSMSource::kind_of(const void* address, std::size_t device_index) const
        {
            require_device(device_index);
            if (pages_->holds(address))
                return USMKind::Device;
            return inner_->kind_of(address, device_index);
        }

        void GuardedUSMSource::require_device(std::size_t index) const
        {
            const std::size_t count = inner_->device_count();
            SR_VERIFY(index < count,
                      ("GuardedUSMSource: device index " + std::to_string(index) +
                       " is out of range: the source holds " + std::to_string(count) + " device(s).")
                          .c_str());
        }

        std::unique_ptr<USMSource> make_guarded_usm_source(std::unique_ptr<USMSource> inner,
                                                           std::shared_ptr<GuardedDevicePages> pages)
        {
            SR_VERIFY(inner != nullptr, "make_guarded_usm_source: pass the source to wrap, not null.");
            SR_VERIFY(pages != nullptr, "make_guarded_usm_source: pass the guarded pages, not null.");
            return std::make_unique<GuardedUSMSource>(std::move(inner), std::move(pages));
        }
    } // namespace Internal::Memory
} // namespace SushiRuntime
