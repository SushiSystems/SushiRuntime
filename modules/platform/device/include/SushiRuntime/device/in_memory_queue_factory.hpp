/**************************************************************************/
/* in_memory_queue_factory.hpp                                            */
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
 * @file in_memory_queue_factory.hpp
 * @brief Declares InMemoryQueueFactory, a QueueFactory of host queues that records each build.
 * @author Mustafa Garip
 * @see modules/platform/device/README.md
 */

#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include <sycl/sycl.hpp>

#include <SushiRuntime/device/queue_factory.hpp>
#include <SushiRuntime/device/queue_flavour.hpp>

namespace SushiRuntime
{
    namespace Internal::Device
    {
        /** @brief One queue an InMemoryQueueFactory built. */
        struct QueueBuild
        {
            std::size_t device = 0; /**< The device the queue was built for. */
            QueueFlavour flavour;   /**< The flavour it was built with. */
            bool shared = false;    /**< True when it was built in a given context. */
        };

        /**
         * @brief Builds every queue on the SYCL CPU device and records each build.
         *
         * It holds no device: each make() builds a queue of its own on the SYCL CPU device,
         * or on the first device of a given context, so the queues of two devices differ. A
         * device needs an in-order plain queue when the list given at construction says so.
         * @see modules/platform/device/README.md
         */
        class InMemoryQueueFactory final : public QueueFactory
        {
            public:
                /**
                 * @param device_count The number of devices.
                 * @param in_order Entry i true makes device i need an in-order plain queue;
                 *                 a missing entry is false.
                 */
                explicit InMemoryQueueFactory(std::size_t device_count,
                                              std::vector<bool> in_order = {})
                    : device_count_(device_count), in_order_(std::move(in_order))
                {
                }

                [[nodiscard]] std::size_t count() const noexcept override { return device_count_; }

                [[nodiscard]] bool needs_in_order(std::size_t device) const override
                {
                    return device < in_order_.size() && in_order_[device];
                }

                [[nodiscard]] std::string driver_version(std::size_t device) const override
                {
                    return "in-memory " + std::to_string(device);
                }

                [[nodiscard]] sycl::queue make(std::size_t device, QueueFlavour flavour,
                                               const sycl::context* shared_context) override
                {
                    {
                        const std::lock_guard<std::mutex> lock(mutex_);
                        builds_.push_back(QueueBuild{device, flavour, shared_context != nullptr});
                    }
                    const sycl::property_list properties = properties_of(flavour);
                    if (shared_context)
                        return sycl::queue(*shared_context, shared_context->get_devices().front(), properties);
                    return sycl::queue(sycl::cpu_selector_v, properties);
                }

                /** @brief Returns every build so far, in the order they ran. */
                [[nodiscard]] std::vector<QueueBuild> builds() const
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    return builds_;
                }

            private:
                /// @brief The number of devices.
                std::size_t device_count_;
                /// @brief Entry i true: device i needs an in-order plain queue.
                std::vector<bool> in_order_;
                /// @brief Guards builds_.
                mutable std::mutex mutex_;
                /// @brief Every build so far.
                std::vector<QueueBuild> builds_;
        };
    } // namespace Internal::Device
} // namespace SushiRuntime
