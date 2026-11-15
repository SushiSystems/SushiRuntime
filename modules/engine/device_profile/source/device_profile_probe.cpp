/**************************************************************************/
/* device_profile_probe.cpp                                               */
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
 * @file device_profile_probe.cpp
 * @brief Implements DeviceProfileProbe.
 * @see modules/engine/device_profile/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/device_profile/device_profile_probe.hpp>

#include <chrono>
#include <cstddef>
#include <string>
#include <utility>

#include <SushiRuntime/core/error.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        namespace
        {
            /** @brief Returns the seconds run takes, from its start to its return. */
            template <class Run>
            [[nodiscard]] double seconds_of(Run&& run)
            {
                const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
                run();
                const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
                return std::chrono::duration<double>(end - start).count();
            }

            /** @brief Frees a device allocation when it leaves scope. */
            class DeviceBytes
            {
                public:
                    DeviceBytes(std::size_t bytes, sycl::queue& queue)
                        : queue_(queue), address_(sycl::malloc_device(bytes, queue))
                    {

                    }

                    /** @brief Allocates bytes of host memory the device of queue reads directly. */
                    DeviceBytes(std::size_t bytes, sycl::queue& queue, sycl::usm::alloc kind)
                        : queue_(queue), address_(sycl::malloc(bytes, queue, kind))
                    {

                    }

                    ~DeviceBytes()
                    {
                        if (address_ != nullptr)
                            sycl::free(address_, queue_);
                    }

                    DeviceBytes(const DeviceBytes&) = delete;
                    DeviceBytes& operator=(const DeviceBytes&) = delete;

                    /** @brief Returns the allocation, or null when the device refused it. */
                    [[nodiscard]] void* address() const noexcept { return address_; }

                private:
                    /// @brief The queue whose context owns the allocation.
                    sycl::queue& queue_;

                    /// @brief The allocation; null when refused.
                    void* address_;
            };

            /** @brief Returns the refusal of an allocation of bytes on a device. */
            [[nodiscard]] Error refused_allocation(std::uint64_t bytes, const DeviceKey& device)
            {
                return Error{ErrorCode::AllocationFailed,
                             "the profile probe cannot allocate " + std::to_string(bytes) + " bytes on " +
                                 to_text(device) + "."};
            }
        } // namespace

        DeviceProfileProbe::DeviceProfileProbe(const ProbeDevices& devices, std::vector<TransferPath*> paths,
                                               std::map<OperationKind, ProbeKernel> kernels)
            : devices_(devices), paths_(std::move(paths)), kernels_(std::move(kernels))
        {

        }

        Result<ProbeDevice> DeviceProfileProbe::device_of(const DeviceKey& key) const
        {
            const std::optional<ProbeDevice> found = devices_.find(key);
            if (!found || found->queue == nullptr || found->space == nullptr)
                return make_error(Error{ErrorCode::InvalidArgument,
                                        "the runtime has no device " + to_text(key) + " to probe."});
            return *found;
        }

        Result<CapacitySample> DeviceProfileProbe::measure_capacity(const DeviceKey& device)
        {
            Result<ProbeDevice> found = device_of(device);
            if (!found)
                return make_error(std::move(found.error()));
            CapacitySample sample;
            sample.capacity_bytes = found.value().space->capacity_bytes();
            sample.reserve_bytes = found.value().space->reserve_bytes();
            return sample;
        }

        Result<CopySample> DeviceProfileProbe::measure_device_copy(const DeviceKey& device, std::uint64_t bytes)
        {
            Result<ProbeDevice> found = device_of(device);
            if (!found)
                return make_error(std::move(found.error()));
            sycl::queue& queue = *found.value().queue;
            const std::size_t length = static_cast<std::size_t>(bytes);
            const DeviceBytes from(length, queue);
            const DeviceBytes to(length, queue);
            if (from.address() == nullptr || to.address() == nullptr)
                return make_error(refused_allocation(2 * bytes, device));

            queue.memcpy(to.address(), from.address(), length).wait();
            CopySample sample;
            sample.bytes = bytes;
            sample.seconds = seconds_of([&]() { queue.memcpy(to.address(), from.address(), length).wait(); });
            return sample;
        }

        Result<CopySample> DeviceProfileProbe::measure_path_copy(const DeviceKey& source,
                                                                 const DeviceKey& destination,
                                                                 std::string_view path_name, std::uint64_t bytes)
        {
            Result<ProbeDevice> from_device = device_of(source);
            if (!from_device)
                return make_error(std::move(from_device.error()));
            Result<ProbeDevice> to_device = device_of(destination);
            if (!to_device)
                return make_error(std::move(to_device.error()));
            const Memory::MemorySpace& from_space = *from_device.value().space;
            const Memory::MemorySpace& to_space = *to_device.value().space;

            TransferPath* chosen = nullptr;
            for (TransferPath* path : paths_)
                if (path->name() == path_name && path->connects(from_space, to_space))
                {
                    chosen = path;
                    break;
                }
            if (chosen == nullptr)
                return make_error(Error{ErrorCode::Unsupported,
                                        "no path named " + std::string(path_name) + " connects " +
                                            to_text(source) + " to " + to_text(destination) + "."});

            const std::size_t length = static_cast<std::size_t>(bytes);
            const DeviceBytes from(length, *from_device.value().queue);
            const DeviceBytes to(length, *to_device.value().queue);
            if (from.address() == nullptr)
                return make_error(refused_allocation(bytes, source));
            if (to.address() == nullptr)
                return make_error(refused_allocation(bytes, destination));

            TransferRequest request;
            request.source = from_space.id();
            request.destination = to_space.id();
            request.from = from.address();
            request.to = to.address();
            request.bytes = length;
            chosen->copy(request, {}).wait();
            CopySample sample;
            sample.bytes = bytes;
            sample.seconds = seconds_of([&]() { chosen->copy(request, {}).wait(); });
            return sample;
        }

        Result<CopySample> DeviceProfileProbe::measure_host_copy(const DeviceKey& device, HostDirection direction,
                                                                 std::uint64_t bytes)
        {
            Result<ProbeDevice> found = device_of(device);
            if (!found)
                return make_error(std::move(found.error()));
            sycl::queue& queue = *found.value().queue;
            const std::size_t length = static_cast<std::size_t>(bytes);
            const DeviceBytes on_device(length, queue);
            const DeviceBytes on_host(length, queue, sycl::usm::alloc::host);
            if (on_device.address() == nullptr || on_host.address() == nullptr)
                return make_error(refused_allocation(2 * bytes, device));

            void* to = direction == HostDirection::ToDevice ? on_device.address() : on_host.address();
            const void* from = direction == HostDirection::ToDevice ? on_host.address() : on_device.address();
            queue.memcpy(to, from, length).wait();
            CopySample sample;
            sample.bytes = bytes;
            sample.seconds = seconds_of([&]() { queue.memcpy(to, from, length).wait(); });
            return sample;
        }

        Result<KernelSample> DeviceProfileProbe::measure_kernel(const DeviceKey& device, OperationKind kind)
        {
            Result<ProbeDevice> found = device_of(device);
            if (!found)
                return make_error(std::move(found.error()));
            const auto registered = kernels_.find(kind);
            if (registered == kernels_.end())
                return make_error(Error{ErrorCode::InvalidArgument,
                                        "no probe kernel is registered for kind " + std::to_string(kind.value) +
                                            "; register one before measuring it."});
            sycl::queue& queue = *found.value().queue;

            Result<std::uint64_t> warm = registered->second(queue);
            if (!warm)
                return make_error(std::move(warm.error()));
            Result<std::uint64_t> operations = make_error(Error{ErrorCode::InvalidState, "the kernel did not run."});
            KernelSample sample;
            sample.seconds = seconds_of([&]() { operations = registered->second(queue); });
            if (!operations)
                return make_error(std::move(operations.error()));
            sample.operations = operations.value();
            return sample;
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
