/**************************************************************************/
/* device_queue_set.hpp                                                   */
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
 * @file device_queue_set.hpp
 * @brief Declares DeviceQueueSet, the device inventory and the SYCL queues built on it on first use.
 * @author Mustafa Garip
 * @see modules/platform/device/README.md
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>
#include <sycl/sycl.hpp>

#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/device/device_descriptor.hpp>
#include <SushiRuntime/device/numa_route_table.hpp>
#include <SushiRuntime/device/queue_factory.hpp>
#include <SushiRuntime/device/queue_flavour.hpp>
#include <SushiRuntime/profile/profile.hpp>

namespace SushiRuntime
{
    namespace Internal::Device
    {
        /**
         * @brief Owns the device inventory and every SYCL queue of every device.
         *
         * Construction builds no queue: each queue is built once, on its first use. A
         * reference the set returns stays valid until the set is destroyed. Every member is
         * safe to call from several threads at once.
         */
        class SUSHIRUNTIME_API DeviceQueueSet final
        {
            public:
                /**
                 * @brief Holds the devices of the descriptors and builds the NUMA routing table.
                 * @param devices The inventory, in the order that fixes each device's index.
                 * @param profiling True to build every plain queue with enable_profiling.
                 * @param collector The collector the set's zones record into; it must outlive the set.
                 */
                DeviceQueueSet(std::vector<DeviceDescriptor> devices, bool profiling,
                               Profile::Collector& collector = Profile::process_collector());

                /**
                 * @brief Holds the factory every queue is built through and the NUMA routing table; builds no queue.
                 * @param devices The inventory, in the order that fixes each device's index.
                 * @param factory Builds every queue; holds one device per entry of @p devices; not null.
                 * @param profiling True to build every plain queue with enable_profiling.
                 * @param collector The collector the set's zones record into; it must outlive the set.
                 */
                DeviceQueueSet(std::vector<DeviceDescription> devices,
                               std::unique_ptr<QueueFactory> factory, bool profiling,
                               Profile::Collector& collector = Profile::process_collector());

                /**
                 * @brief Builds a set of host devices that holds no queue and makes no device.
                 *
                 * count(), devices() and the NUMA routes answer as for any set; a call that
                 * returns a queue stops the process, and wait_all() has nothing to wait on.
                 * @param host_devices The inventory, in the order that fixes each device's index.
                 * @param collector The collector the set's zones record into; it must outlive the set.
                 */
                explicit DeviceQueueSet(std::vector<DeviceDescription> host_devices,
                                        Profile::Collector& collector = Profile::process_collector());

                /** @brief Waits for every built queue; logs a failure and never throws. */
                ~DeviceQueueSet();

                DeviceQueueSet(const DeviceQueueSet&) = delete;
                DeviceQueueSet& operator=(const DeviceQueueSet&) = delete;
                DeviceQueueSet(DeviceQueueSet&&) = delete;
                DeviceQueueSet& operator=(DeviceQueueSet&&) = delete;

                /** @brief Returns the number of devices in the set. */
                [[nodiscard]] std::size_t count() const noexcept;

                /** @brief Returns the inventory; entry i describes device i. */
                [[nodiscard]] const std::vector<DeviceDescription>& devices() const noexcept;

                /**
                 * @brief Returns whether a device is a GPU.
                 * @pre index names a device of the set.
                 */
                [[nodiscard]] bool is_gpu(std::size_t index) const;

                /**
                 * @brief Returns the plain queue of a device, building it on first use.
                 * @pre index names a device of the set.
                 */
                [[nodiscard]] sycl::queue& queue(std::size_t index);

                /**
                 * @brief Returns whether two devices share one SYCL context.
                 *
                 * Builds the two plain queues when they are not built yet. The devices of a host
                 * set share the host's one memory, so the answer is true.
                 * @pre first and second name devices of the set.
                 */
                [[nodiscard]] bool shares_context(std::size_t first, std::size_t second) const;

                /**
                 * @brief Returns the free memory of a device in bytes, its total when the
                 *        runtime has no query for it, and 0 when it cannot be read.
                 *
                 * Builds the device's plain queue when it is not built yet. A host device
                 * answers the memory its description states.
                 * @pre index names a device of the set.
                 */
                [[nodiscard]] std::uint64_t free_memory(std::size_t index) const;

                /**
                 * @brief Returns a device's queue of one flavour, building it on first use.
                 * @param flavour Properties asked for; a device whose plain queue is in-order
                 *                gets an in-order queue whatever is asked.
                 * @pre index names a device of the set.
                 * @see modules/platform/device/README.md
                 */
                [[nodiscard]] sycl::queue& queue(std::size_t index, QueueFlavour flavour);

                /**
                 * @brief Returns whether a profiling queue of a device exists; builds nothing.
                 * @return False on a bad index.
                 */
                [[nodiscard]] bool has_profiling_queue(std::size_t index) const noexcept;

                /**
                 * @brief Returns the lowest-indexed device of a NUMA node, or device 0 when the
                 *        node has none.
                 * @pre The set holds a device.
                 */
                [[nodiscard]] std::size_t device_for_numa(int numa_node) const;

                /**
                 * @brief Returns a device's plain queue, or one rotated over a NUMA node's devices.
                 * @param device The device to use; nothing selects the rotation.
                 * @param fallback_numa NUMA node the rotation runs over when @p device is empty.
                 * @pre device, when given, names a device of the set, and the set holds one.
                 * @see modules/platform/device/README.md
                 */
                [[nodiscard]] sycl::queue& queue_or_numa_local(std::optional<std::size_t> device,
                                                               int fallback_numa);

                /** @brief Returns a copy of every plain queue handle, in device order, building
                 *         the plain queues not built yet. */
                [[nodiscard]] std::vector<sycl::queue> plain_queues() const;

                /**
                 * @brief Waits for every built queue of every flavour.
                 * @return Nothing, or a KernelFailed error with the text of the first
                 *         asynchronous error a queue reports; the queues after it are still
                 *         waited for.
                 */
                [[nodiscard]] Result<void> wait_all();

                /**
                 * @brief Makes every built queue report the asynchronous errors it holds,
                 *        without waiting.
                 * @return Nothing, or a KernelFailed error with the text of the first one.
                 */
                [[nodiscard]] Result<void> raise_asynchronous_errors();

            private:
                /** @brief Holds one queue of the table and the guard that builds it once. */
                struct LazyQueue
                {
                    std::optional<sycl::queue> queue; /**< Empty until once fires. */
                    std::once_flag once;              /**< Guards the single build of queue. */
                    std::atomic<bool> built{false};   /**< Set after queue is constructed. */
                };

                /** @brief Counts the flavours: in-order or not, times profiling or not. */
                static constexpr std::size_t K_FLAVOUR_COUNT = 4;

                /**
                 * @brief Stops the process when an index names no device.
                 */
                void require_index(std::size_t index) const;

                /**
                 * @brief Stops the process when an index names no device or a host device.
                 */
                void require_queue(std::size_t index) const;

                /**
                 * @brief Returns the offset, among a device's slots, of the slot that serves a
                 *        flavour.
                 * @pre index < count().
                 */
                [[nodiscard]] std::size_t offset_for(std::size_t index,
                                                     QueueFlavour flavour) const noexcept;

                /**
                 * @brief Returns the plain queue of a device, building it once on first use.
                 * @pre index < count() and the set holds a factory.
                 */
                [[nodiscard]] sycl::queue& plain_queue(std::size_t index) const;

                /**
                 * @brief Builds one queue into a slot through the factory and logs the build.
                 * @param shared_context Context to build in; null gives the queue its own.
                 * @pre Called once per slot, under the slot's once flag.
                 */
                void build_slot(LazyQueue& slot, std::size_t index, std::size_t offset,
                                const sycl::context* shared_context) const;

                /** @brief Logs the NUMA routes of the set once they are built. */
                void log_numa_routes() const;

                /** @brief Calls visit(device info, queue) for every queue built so far. */
                template <typename Visitor>
                void for_each_built_queue(Visitor&& visit);

                /**
                 * @brief Calls visit(queue) for every queue built so far and turns what a call
                 *        throws into an Error.
                 * @return Nothing, or a KernelFailed error with the text of the first throw.
                 */
                template <typename Visit>
                [[nodiscard]] Result<void> first_failure_of(Visit&& visit);

                /// @brief Inventory; entry i describes device i.
                std::vector<DeviceDescription> infos_;

                /// @brief Per device, the offset of its plain queue among the device's slots.
                std::vector<std::uint8_t> plain_offsets_;

                /// @brief Queue table, K_FLAVOUR_COUNT slots per device, each filled on first use.
                mutable std::vector<LazyQueue> slots_;

                /// @brief Routes of every NUMA node to the devices that serve it.
                NUMARouteTable numa_routes_;

                /// @brief Builds every queue of the set; null for a set of host devices.
                std::unique_ptr<QueueFactory> factory_;

                /** @brief The collector the set's zones record into. */
                Profile::Collector& collector_;
        };
    } // namespace Device
} // namespace SushiRuntime
