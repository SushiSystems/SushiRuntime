/**************************************************************************/
/* device_queue_set.cpp                                                   */
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
 * @file device_queue_set.cpp
 * @brief Builds the SYCL queues of every device and routes NUMA nodes to devices.
 * @author Mustafa Garip
 * @see modules/platform/device/README.md
 */

#include <SushiRuntime/device/device_queue_set.hpp>

#include <exception>
#include <string>
#include <utility>

#include <SushiRuntime/core/common.hpp>
#include <SushiRuntime/core/assert.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/logging/logger.hpp>

namespace SushiRuntime
{
    namespace Internal::Device
    {
        namespace
        {
            /** @brief Marks the in-order half of a device's slots. */
            constexpr std::size_t K_IN_ORDER_OFFSET = 2;

            /** @brief Marks the profiling slot within each half. */
            constexpr std::size_t K_PROFILING_OFFSET = 1;

            /** @brief Returns the offset of a flavour among a device's slots. */
            constexpr std::size_t offset_of(QueueFlavour flavour) noexcept
            {
                return (flavour.in_order ? K_IN_ORDER_OFFSET : 0)
                     + (flavour.profiling ? K_PROFILING_OFFSET : 0);
            }

            /** @brief Returns the flavour a slot offset stands for. */
            constexpr QueueFlavour flavour_of(std::size_t offset) noexcept
            {
                return QueueFlavour{(offset & K_IN_ORDER_OFFSET) != 0,
                                    (offset & K_PROFILING_OFFSET) != 0};
            }

            /** @brief Rethrows every asynchronous error a queue collected. */
            void rethrow_async_errors(sycl::exception_list errors)
            {
                for (const std::exception_ptr& error : errors)
                    std::rethrow_exception(error);
            }

            /** @brief Returns the SYCL property list of a flavour. */
            sycl::property_list properties_of(QueueFlavour flavour)
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

            /**
             * @brief Builds one queue of a flavour on a device.
             * @param shared_context Context to build in; null gives the queue its own.
             */
            sycl::queue make_queue(const sycl::device& device, const sycl::context* shared_context,
                                   QueueFlavour flavour)
            {
                if (shared_context)
                    return sycl::queue(*shared_context, device, rethrow_async_errors,
                                       properties_of(flavour));
                return sycl::queue(device, rethrow_async_errors, properties_of(flavour));
            }

            /**
             * @brief Returns this thread's next rotation position and advances it.
             * @param is_fallback True to advance the one cursor every device-less node shares.
             * @pre numa_node is in [0, K_MAX_NUMA_NODES) unless is_fallback.
             */
            std::size_t next_rotation(bool is_fallback, int numa_node) noexcept
            {
                static thread_local std::size_t numa_cursors[K_MAX_NUMA_NODES] = {0};
                static thread_local std::size_t fallback_cursor = 0;

                if (is_fallback)
                    return fallback_cursor++;
                return numa_cursors[static_cast<std::size_t>(numa_node)]++;
            }

            /** @brief Builds the exception an operation on an empty set throws. */
            SushiException empty_set_error()
            {
                return SushiException(Error{
                    ErrorCode::Unsupported,
                    "The device set holds no device, so no NUMA node resolves to one."});
            }
        } // namespace

        template <typename Visitor>
        void DeviceQueueSet::for_each_built_queue(Visitor&& visit)
        {
            for (std::size_t position = 0; position < slots_.size(); ++position)
            {
                LazyQueue& slot = slots_[position];
                if (slot.built.load(std::memory_order_acquire))
                    visit(infos_[position / K_FLAVOUR_COUNT], *slot.queue);
            }
        }

        DeviceQueueSet::DeviceQueueSet(std::vector<DeviceDescriptor> devices, bool profiling)
        {
            const std::size_t device_count = devices.size();

            infos_.reserve(device_count);
            plain_offsets_.reserve(device_count);
            // Move-assigned, not resized: once_flag and atomic elements are not movable.
            slots_ = std::vector<LazyQueue>(device_count * K_FLAVOUR_COUNT);

            for (std::size_t index = 0; index < device_count; ++index)
            {
                DeviceDescriptor& descriptor = devices[index];
                DeviceDescription& info = descriptor.info;
                info.index = index;

                if (info.numa_node < 0
                    || static_cast<std::size_t>(info.numa_node) >= K_MAX_NUMA_NODES)
                {
                    SR_LOG_WARN("Device {} reported invalid NUMA {}. Falling back to NUMA 0 for "
                                "queue mapping.",
                                info.name, info.numa_node);
                    info.numa_node = 0;
                }

                // A non-CPU, non-GPU device gets an in-order queue (device/README.md).
                const QueueFlavour plain{
                    !descriptor.device.is_cpu() && !descriptor.device.is_gpu(), profiling};
                const std::size_t offset = offset_of(plain);

                LazyQueue& slot = slots_[index * K_FLAVOUR_COUNT + offset];
                std::call_once(slot.once, [&slot, &descriptor, plain]()
                {
                    slot.queue.emplace(make_queue(descriptor.device, nullptr, plain));
                    slot.built.store(true, std::memory_order_release);
                });

                SR_LOG_INFO("Device: {} | NUMA: {} | Memory: {} MB | Compute units: {}",
                            info.name, info.numa_node, info.memory_bytes / (1024 * 1024),
                            info.compute_units);

                plain_offsets_.push_back(static_cast<std::uint8_t>(offset));
                infos_.push_back(std::move(info));
            }

            build_numa_routes();
        }

        DeviceQueueSet::~DeviceQueueSet()
        {
            for_each_built_queue([]([[maybe_unused]] const DeviceDescription& info, sycl::queue& built)
            {
                // A late async device error is logged, never thrown (device/README.md).
                try
                {
                    built.wait_and_throw();
                }
                catch (const std::exception& error)
                {
                    SR_LOG_WARN("Async device error on device {} ({}) during shutdown drain: {}; "
                                "the queue is released anyway.",
                                info.index, info.name, error.what());
                }
                catch (...)
                {
                    SR_LOG_WARN("Unknown async device error on device {} ({}) during shutdown "
                                "drain; the queue is released anyway.",
                                info.index, info.name);
                }
            });
        }

        std::size_t DeviceQueueSet::count() const noexcept
        {
            return infos_.size();
        }

        const std::vector<DeviceDescription>& DeviceQueueSet::devices() const noexcept
        {
            return infos_;
        }

        bool DeviceQueueSet::is_gpu(std::size_t index) const
        {
            require_index(index);
            return infos_[index].kind == DeviceKind::Gpu;
        }

        sycl::queue& DeviceQueueSet::queue(std::size_t index)
        {
            require_index(index);
            return *slots_[index * K_FLAVOUR_COUNT + plain_offsets_[index]].queue;
        }

        sycl::queue& DeviceQueueSet::queue(std::size_t index, QueueFlavour flavour)
        {
            require_index(index);

            const std::size_t offset = offset_for(index, flavour);
            LazyQueue& slot = slots_[index * K_FLAVOUR_COUNT + offset];
            std::call_once(slot.once, [this, index, offset, &slot]()
            {
                const sycl::queue& plain = *plain_slot(index).queue;
                const sycl::context context = plain.get_context();

                slot.queue.emplace(make_queue(plain.get_device(), &context, flavour_of(offset)));
                // Stored last: readers that skip call_once read only this flag.
                slot.built.store(true, std::memory_order_release);
            });
            return *slot.queue;
        }

        bool DeviceQueueSet::has_profiling_queue(std::size_t index) const noexcept
        {
            if (index >= infos_.size())
                return false;

            const LazyQueue* device_slots = &slots_[index * K_FLAVOUR_COUNT];
            return device_slots[K_PROFILING_OFFSET].built.load(std::memory_order_acquire)
                || device_slots[K_IN_ORDER_OFFSET + K_PROFILING_OFFSET].built.load(
                       std::memory_order_acquire);
        }

        std::size_t DeviceQueueSet::device_for_numa(int numa_node) const
        {
            const NUMARoute& route = route_for(numa_node);
            if (route.devices.empty())
                throw empty_set_error();
            return route.devices.front();
        }

        sycl::queue& DeviceQueueSet::queue_or_numa_local(std::optional<std::size_t> device,
                                                         int fallback_numa)
        {
            return queue(device ? *device : next_device_for_numa(fallback_numa));
        }

        std::vector<sycl::queue> DeviceQueueSet::plain_queues() const
        {
            std::vector<sycl::queue> queues;
            queues.reserve(infos_.size());
            for (std::size_t index = 0; index < infos_.size(); ++index)
                queues.push_back(*plain_slot(index).queue);
            return queues;
        }

        Result<void> DeviceQueueSet::wait_all()
        {
            Error first;
            for_each_built_queue([&first](const DeviceDescription&, sycl::queue& built)
            {
                // The asynchronous handler rethrows here, where the wait is.
                try
                {
                    built.wait_and_throw();
                }
                catch (const std::exception& failure)
                {
                    if (first.code == ErrorCode::Ok)
                        first = Detail::error_without_throwing(ErrorCode::KernelFailed,
                                                               failure.what());
                }
                catch (...)
                {
                    if (first.code == ErrorCode::Ok)
                        first = Detail::error_without_throwing(
                            ErrorCode::KernelFailed,
                            "A device queue reported an error that is not a std::exception.");
                }
            });
            if (first.code != ErrorCode::Ok)
                return make_error(std::move(first));
            return {};
        }

        void DeviceQueueSet::require_index(std::size_t index) const
        {
            SR_VERIFY(index < infos_.size(),
                      ("Device index " + std::to_string(index) +
                       " is out of bounds; the set holds " + std::to_string(infos_.size()) +
                       " device(s).")
                          .c_str());
        }

        std::size_t DeviceQueueSet::offset_for(std::size_t index,
                                               QueueFlavour flavour) const noexcept
        {
            // A device whose plain queue is in-order never gets an out-of-order one.
            const std::size_t forced = plain_offsets_[index] & K_IN_ORDER_OFFSET;
            return offset_of(flavour) | forced;
        }

        const DeviceQueueSet::LazyQueue&
        DeviceQueueSet::plain_slot(std::size_t index) const noexcept
        {
            return slots_[index * K_FLAVOUR_COUNT + plain_offsets_[index]];
        }

        const DeviceQueueSet::NUMARoute& DeviceQueueSet::route_for(int numa_node) const noexcept
        {
            if (numa_node < 0 || static_cast<std::size_t>(numa_node) >= numa_routes_.size())
                return outside_route_;
            return numa_routes_[static_cast<std::size_t>(numa_node)];
        }

        std::size_t DeviceQueueSet::next_device_for_numa(int numa_node) const
        {
            const NUMARoute& route = route_for(numa_node);
            if (route.devices.empty())
                throw empty_set_error();

            const std::size_t position = next_rotation(route.is_fallback, numa_node);
            return route.devices[position % route.devices.size()];
        }

        void DeviceQueueSet::build_numa_routes()
        {
            outside_route_.is_fallback = true;
            outside_route_.devices.reserve(infos_.size());

            std::size_t node_count = 0;
            for (const DeviceDescription& info : infos_)
            {
                outside_route_.devices.push_back(info.index);
                // numa_node was clamped to [0, K_MAX_NUMA_NODES) by the constructor.
                const std::size_t node = static_cast<std::size_t>(info.numa_node);
                if (node + 1 > node_count)
                    node_count = node + 1;
            }

            numa_routes_.resize(node_count);
            for (const DeviceDescription& info : infos_)
            {
                NUMARoute& route = numa_routes_[static_cast<std::size_t>(info.numa_node)];
                route.devices.push_back(info.index);
            }

            [[maybe_unused]] std::size_t populated = 0;
            std::string empty_nodes;
            for (std::size_t node = 0; node < node_count; ++node)
            {
                if (!numa_routes_[node].devices.empty())
                {
                    ++populated;
                    continue;
                }

                numa_routes_[node] = outside_route_;
                if (!empty_nodes.empty())
                    empty_nodes += ", ";
                empty_nodes += std::to_string(node);
            }

            if (!empty_nodes.empty())
                SR_LOG_WARN("No device sits on NUMA node(s) {}; a request for one of them is "
                            "served from all {} devices in rotation.",
                            empty_nodes, infos_.size());

            SR_LOG_INFO("Device set ready. Total devices: {}, NUMA domains: {}. A NUMA id outside "
                        "[0, {}) is served from all devices in rotation.",
                        infos_.size(), populated, node_count);
        }
    } // namespace Device
} // namespace SushiRuntime
