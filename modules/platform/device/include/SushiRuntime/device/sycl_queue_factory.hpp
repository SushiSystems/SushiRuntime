/**************************************************************************/
/* sycl_queue_factory.hpp                                                 */
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
 * @file sycl_queue_factory.hpp
 * @brief Declares SYCLQueueFactory, the QueueFactory that builds queues on SYCL devices.
 * @author Mustafa Garip
 * @see modules/platform/device/README.md
 */

#pragma once

#include <cstddef>
#include <string>
#include <vector>
#include <sycl/sycl.hpp>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/device/queue_factory.hpp>
#include <SushiRuntime/device/queue_flavour.hpp>

namespace SushiRuntime
{
    namespace Internal::Device
    {
        /**
         * @brief Builds queues on a list of SYCL devices; every queue rethrows its
         *        asynchronous errors when asked for them.
         *
         * A device that is neither a CPU nor a GPU needs an in-order plain queue.
         * @see modules/platform/device/README.md
         */
        class SUSHIRUNTIME_API SYCLQueueFactory final : public QueueFactory
        {
            public:
                /** @param devices Device i of the factory is entry i. */
                explicit SYCLQueueFactory(std::vector<sycl::device> devices);

                [[nodiscard]] std::size_t count() const noexcept override;
                [[nodiscard]] bool needs_in_order(std::size_t device) const override;
                [[nodiscard]] std::string driver_version(std::size_t device) const override;
                [[nodiscard]] sycl::queue make(std::size_t device, QueueFlavour flavour,
                                               const sycl::context* shared_context) override;

            private:
                /// @brief Device i of the factory.
                std::vector<sycl::device> devices_;
        };
    } // namespace Internal::Device
} // namespace SushiRuntime
