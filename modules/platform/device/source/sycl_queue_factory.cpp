/**************************************************************************/
/* sycl_queue_factory.cpp                                                 */
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
 * @file sycl_queue_factory.cpp
 * @brief Defines SYCLQueueFactory.
 * @author Mustafa Garip
 * @see modules/platform/device/README.md
 */

#include <SushiRuntime/device/sycl_queue_factory.hpp>

#include <exception>
#include <utility>

#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/device/device_log_category.hpp>

namespace SushiRuntime
{
    namespace Internal::Device
    {
        namespace
        {
            /** @brief Rethrows every asynchronous error a queue collected. */
            void rethrow_async_errors(sycl::exception_list errors)
            {
                for (const std::exception_ptr& error : errors)
                    std::rethrow_exception(error);
            }
        } // namespace

        SYCLQueueFactory::SYCLQueueFactory(std::vector<sycl::device> devices)
            : devices_(std::move(devices))
        {
        }

        std::size_t SYCLQueueFactory::count() const noexcept
        {
            return devices_.size();
        }

        bool SYCLQueueFactory::needs_in_order(std::size_t device) const
        {
            const sycl::device& target = devices_.at(device);
            return !target.is_cpu() && !target.is_gpu();
        }

        std::string SYCLQueueFactory::driver_version(std::size_t device) const
        {
            const sycl::device& target = devices_.at(device);
            try
            {
                return target.get_info<sycl::info::device::driver_version>();
            }
            catch (const sycl::exception& error)
            {
                SR_LOG_DEBUG(K_DEVICE_LOG_CATEGORY,
                             "Driver version of device {} is not answerable: {}; the device "
                             "line names it unknown.",
                             target.get_info<sycl::info::device::name>(), error.what());
                return "unknown";
            }
        }

        sycl::queue SYCLQueueFactory::make(std::size_t device, QueueFlavour flavour,
                                           const sycl::context* shared_context)
        {
            const sycl::device& target = devices_.at(device);
            if (shared_context)
                return sycl::queue(*shared_context, target, rethrow_async_errors,
                                   properties_of(flavour));
            return sycl::queue(target, rethrow_async_errors, properties_of(flavour));
        }
    } // namespace Internal::Device
} // namespace SushiRuntime
