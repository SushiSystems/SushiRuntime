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
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
/**************************************************************************/

/**
 * @file device_queue_set.hpp
 * @brief Declares DeviceQueueSet, the device inventory and the SYCL queues built on it.
 * @author Mustafa Garip
 * @see include/SushiRuntime/device/README.md
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>
#include <sycl/sycl.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/device/device_info.hpp>
#include <SushiRuntime/device/queue_flavour.hpp>

namespace SushiRuntime
{
    namespace Device
    {
        /**
         * @brief Owns the device inventory and every SYCL queue of every device.
         *
         * The set is immutable after construction, so a reference it returns stays
         * valid until the set is destroyed. Every member is safe to call from several
         * threads at once.
         */
        class SUSHIRUNTIME_API DeviceQueueSet final
        {
            public:
                /**
                 * @brief Builds the plain queue of every device and the NUMA routing table.
                 * @param devices The inventory, in the order that fixes each device's index.
                 * @param profiling True to build every plain queue with enable_profiling.
                 */
                DeviceQueueSet(std::vector<DeviceDescriptor> devices, bool profiling);

                /** @brief Waits for every built queue; logs a failure and never throws. */
                ~DeviceQueueSet();

                DeviceQueueSet(const DeviceQueueSet&) = delete;
                DeviceQueueSet& operator=(const DeviceQueueSet&) = delete;
                DeviceQueueSet(DeviceQueueSet&&) = delete;
                DeviceQueueSet& operator=(DeviceQueueSet&&) = delete;

                /** @brief Returns the number of devices in the set. */
                [[nodiscard]] std::size_t count() const noexcept;

                /** @brief Returns the inventory; entry i describes device i. */
                [[nodiscard]] const std::vector<DeviceInfo>& devices() const noexcept;

                /**
                 * @brief Returns whether a device is a GPU.
                 * @throws Core::SushiException With Errc::invalid_access on a bad index.
                 */
                [[nodiscard]] bool is_gpu(std::size_t index) const;

                /**
                 * @brief Returns the plain queue of a device, built at construction.
                 * @throws Core::SushiException With Errc::invalid_access on a bad index.
                 */
                [[nodiscard]] sycl::queue& queue(std::size_t index);

                /**
                 * @brief Returns a device's queue of one flavour, building it on first use.
                 * @param flavour Properties asked for; a device whose plain queue is in-order
                 *                gets an in-order queue whatever is asked.
                 * @throws Core::SushiException With Errc::invalid_access on a bad index.
                 * @see include/SushiRuntime/device/README.md
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
                 * @throws Core::SushiException With Errc::unsupported when the set is empty.
                 */
                [[nodiscard]] std::size_t device_for_numa(int numa_node) const;

                /**
                 * @brief Returns a device's plain queue, or one rotated over a NUMA node's devices.
                 * @param device The device to use; nothing selects the rotation.
                 * @param fallback_numa NUMA node the rotation runs over when @p device is empty.
                 * @throws Core::SushiException On a bad index, or when the set is empty.
                 * @see include/SushiRuntime/device/README.md
                 */
                [[nodiscard]] sycl::queue& queue_or_numa_local(std::optional<std::size_t> device,
                                                               int fallback_numa);

                /** @brief Returns a copy of every plain queue handle, in device order. */
                [[nodiscard]] std::vector<sycl::queue> plain_queues() const;

                /**
                 * @brief Waits for every built queue of every flavour.
                 * @throws sycl::exception If a queue reports an asynchronous error.
                 */
                void wait_all();

            private:
                /** @brief Holds one queue of the table and the guard that builds it once. */
                struct LazyQueue
                {
                    std::optional<sycl::queue> queue; /**< Empty until once fires. */
                    std::once_flag once;              /**< Guards the single build of queue. */
                    std::atomic<bool> built{false};   /**< Set after queue is constructed. */
                };

                /** @brief Lists the devices a NUMA node's requests are served from. */
                struct NumaRoute
                {
                    std::vector<std::size_t> devices; /**< Device indices, ascending. */
                    bool is_fallback = false;         /**< True when the node has no device. */
                };

                /** @brief Counts the flavours: in-order or not, times profiling or not. */
                static constexpr std::size_t K_FLAVOUR_COUNT = 4;

                /**
                 * @brief Throws when an index names no device.
                 * @throws Core::SushiException With Errc::invalid_access.
                 */
                void require_index(std::size_t index) const;

                /**
                 * @brief Returns the offset, among a device's slots, of the slot that serves a
                 *        flavour.
                 * @pre index < count().
                 */
                [[nodiscard]] std::size_t offset_for(std::size_t index,
                                                     QueueFlavour flavour) const noexcept;

                /**
                 * @brief Returns the plain-queue slot of a device.
                 * @pre index < count().
                 */
                [[nodiscard]] const LazyQueue& plain_slot(std::size_t index) const noexcept;

                /** @brief Returns the route of a NUMA node; any id is accepted. */
                [[nodiscard]] const NumaRoute& route_for(int numa_node) const noexcept;

                /**
                 * @brief Returns the next device of a NUMA node's rotation on this thread.
                 * @throws Core::SushiException With Errc::unsupported when the set is empty.
                 */
                [[nodiscard]] std::size_t next_device_for_numa(int numa_node) const;

                /** @brief Fills numa_routes_ and outside_route_ from infos_, and logs them. */
                void build_numa_routes();

                /** @brief Calls visit(device info, queue) for every queue built so far. */
                template <typename Visitor>
                void for_each_built_queue(Visitor&& visit);

                /// @brief Inventory; entry i describes device i.
                std::vector<DeviceInfo> infos_;

                /// @brief Per device, the offset of its plain queue among the device's slots.
                std::vector<std::uint8_t> plain_offsets_;

                /// @brief Queue table, K_FLAVOUR_COUNT slots per device; never resized.
                std::vector<LazyQueue> slots_;

                /// @brief Route of each NUMA node from 0 to the highest one that holds a device.
                std::vector<NumaRoute> numa_routes_;

                /// @brief Route of every NUMA id outside numa_routes_: all devices.
                NumaRoute outside_route_;
        };
    } // namespace Device
} // namespace SushiRuntime
