/**************************************************************************/
/* queue_factory.hpp                                                      */
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
 * @file queue_factory.hpp
 * @brief Declares QueueFactory, the port a DeviceQueueSet builds its queues through.
 * @author Mustafa Garip
 * @see modules/platform/device/README.md
 */

#pragma once

#include <cstddef>
#include <string>
#include <sycl/sycl.hpp>

#include <SushiRuntime/device/queue_flavour.hpp>

namespace SushiRuntime
{
    namespace Internal::Device
    {
        /**
         * @brief Builds the queues of a fixed list of devices, device i at index i.
         * @see modules/platform/device/README.md
         */
        class QueueFactory
        {
            public:
                virtual ~QueueFactory() = default;

                /** @brief Returns the number of devices the factory builds queues for. */
                [[nodiscard]] virtual std::size_t count() const noexcept = 0;

                /**
                 * @brief Returns whether a device's plain queue must run in order.
                 * @pre device < count().
                 */
                [[nodiscard]] virtual bool needs_in_order(std::size_t device) const = 0;

                /**
                 * @brief Returns the driver version of a device, or "unknown" when it cannot.
                 * @pre device < count().
                 */
                [[nodiscard]] virtual std::string driver_version(std::size_t device) const = 0;

                /**
                 * @brief Builds one queue of a flavour on a device.
                 * @param shared_context Context to build in; null gives the queue its own.
                 * @pre device < count().
                 */
                [[nodiscard]] virtual sycl::queue make(std::size_t device, QueueFlavour flavour,
                                                       const sycl::context* shared_context) = 0;

            protected:
                QueueFactory() = default;
                QueueFactory(const QueueFactory&) = default;
                QueueFactory& operator=(const QueueFactory&) = default;
        };

        /** @brief Returns the SYCL property list a queue of a flavour is built with. */
        [[nodiscard]] inline sycl::property_list properties_of(QueueFlavour flavour)
        {
            if (flavour.in_order && flavour.profiling)
                return {sycl::property::queue::in_order(),
                        sycl::property::queue::enable_profiling()};
            if (flavour.in_order)
                return {sycl::property::queue::in_order()};
            if (flavour.profiling)
                return {sycl::property::queue::enable_profiling()};
            return {};
        }
    } // namespace Internal::Device
} // namespace SushiRuntime
