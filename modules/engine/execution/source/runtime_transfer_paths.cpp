/**************************************************************************/
/* runtime_transfer_paths.cpp                                             */
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
 * @file runtime_transfer_paths.cpp
 * @brief Implements build_transfer_paths.
 * @see modules/engine/execution/README.md
 * @author Mustafa Garip
 */

#include "runtime_transfer_paths.hpp"

#include <cstddef>
#include <optional>
#include <utility>

#include <SushiRuntime/task/handle_location.hpp>
#include <SushiRuntime/transfer/host_staged_path.hpp>
#include <SushiRuntime/transfer/peer_access.hpp>
#include <SushiRuntime/transfer/peer_path.hpp>
#include <SushiRuntime/transfer/pinned_host_memory.hpp>
#include <SushiRuntime/transfer/same_context_path.hpp>
#include <SushiRuntime/transfer/space_queues.hpp>
#include <SushiRuntime/transfer/staging_ring.hpp>

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        namespace
        {
            /** @brief Returns the device a space belongs to when the set holds it. */
            std::optional<std::size_t> device_in(const Device::DeviceQueueSet& devices, MemorySpaceID space) noexcept
            {
                const std::optional<std::size_t> device = Graph::device_of_space(space);
                if (device && *device < devices.count())
                    return device;
                return std::nullopt;
            }

            /** @brief Names the plain queue of each device of a set by the device's space. */
            class DeviceSpaceQueues final : public Coherence::SpaceQueues
            {
                public:
                    explicit DeviceSpaceQueues(Device::DeviceQueueSet& devices) noexcept : devices_(&devices)
                    {

                    }

                    [[nodiscard]] sycl::queue* queue_of(MemorySpaceID space) const override
                    {
                        const std::optional<std::size_t> device = device_in(*devices_, space);
                        return device ? &devices_->queue(*device) : nullptr;
                    }

                private:
                    /// @brief The devices whose queues are named.
                    Device::DeviceQueueSet* devices_;
            };

            /** @brief Grants peer access through the SYCL peer-access extension where the compiler has it. */
            class DevicePeerAccess final : public Coherence::PeerAccess
            {
                public:
                    explicit DevicePeerAccess(Device::DeviceQueueSet& devices) noexcept : devices_(&devices)
                    {

                    }

                    [[nodiscard]] bool grant(MemorySpaceID reader, MemorySpaceID owner) override
                    {
#if defined(SYCL_IMPLEMENTATION_INTEL)
                        const std::optional<std::size_t> reading = device_in(*devices_, reader);
                        const std::optional<std::size_t> owning = device_in(*devices_, owner);
                        if (!reading || !owning)
                            return false;
                        try
                        {
                            sycl::device reading_device = devices_->queue(*reading).get_device();
                            const sycl::device owning_device = devices_->queue(*owning).get_device();
                            if (reading_device.get_backend() != owning_device.get_backend() ||
                                !reading_device.ext_oneapi_can_access_peer(owning_device))
                                return false;
                            reading_device.ext_oneapi_enable_peer_access(owning_device);
                            return true;
                        }
                        catch (const sycl::exception&)
                        {
                            return false;
                        }
#else
                        static_cast<void>(reader);
                        static_cast<void>(owner);
                        return false;
#endif
                    }

                private:
                    /// @brief The devices whose peers are asked.
                    Device::DeviceQueueSet* devices_;
            };

            /** @brief Makes pinned host memory in the context of the first device of a set. */
            class DevicePinnedHostMemory final : public Coherence::PinnedHostMemory
            {
                public:
                    explicit DevicePinnedHostMemory(Device::DeviceQueueSet& devices) noexcept : devices_(&devices)
                    {

                    }

                    [[nodiscard]] void* allocate(std::size_t bytes) override
                    {
                        if (devices_->count() == 0)
                            return nullptr;
                        try
                        {
                            return sycl::malloc_host(bytes, devices_->queue(0));
                        }
                        catch (const sycl::exception&)
                        {
                            return nullptr;
                        }
                    }

                    void release(void* address) noexcept override
                    {
                        sycl::free(address, devices_->queue(0));
                    }

                private:
                    /// @brief The devices whose first context holds the memory.
                    Device::DeviceQueueSet* devices_;
            };
        } // namespace

        /** @brief Holds the queues and the access the paths of a RuntimeTransferPaths refer to. */
        struct RuntimeTransferPaths::Support
        {
            explicit Support(Device::DeviceQueueSet& devices)
                : queues(devices), access(devices), pinned(devices), ring(pinned)
            {

            }

            DeviceSpaceQueues queues;         /**< Names the queue of each space. */
            DevicePeerAccess access;          /**< Grants peer access between devices. */
            DevicePinnedHostMemory pinned;    /**< Makes the staging slots. */
            Coherence::StagingRing ring;      /**< Holds the staging slots; sized by the default shape. */
        };

        RuntimeTransferPaths::RuntimeTransferPaths() = default;
        RuntimeTransferPaths::RuntimeTransferPaths(RuntimeTransferPaths&&) noexcept = default;
        RuntimeTransferPaths& RuntimeTransferPaths::operator=(RuntimeTransferPaths&&) noexcept = default;
        RuntimeTransferPaths::~RuntimeTransferPaths() = default;

        RuntimeTransferPaths build_transfer_paths(Device::DeviceQueueSet& devices,
                                                  Coherence::TransferObserver* observer)
        {
            RuntimeTransferPaths paths;
            paths.support = std::make_unique<RuntimeTransferPaths::Support>(devices);
            auto file = std::make_unique<Coherence::FilePath>(paths.support->queues, paths.support->ring, observer);
            Coherence::FilePath* const file_path = file.get();

            std::vector<std::unique_ptr<Coherence::TransferPath>> built;
            built.push_back(std::make_unique<Coherence::SameContextPath>(paths.support->queues, observer));
            built.push_back(
                std::make_unique<Coherence::PeerPath>(paths.support->queues, paths.support->access, observer));
            built.push_back(
                std::make_unique<Coherence::HostStagedPath>(paths.support->queues, paths.support->ring, observer));
            built.push_back(std::move(file));

            std::vector<Coherence::TransferPath*> candidates;
            for (std::unique_ptr<Coherence::TransferPath>& path : built)
            {
                candidates.push_back(path.get());
                paths.owned.push_back(std::move(path));
            }
            paths.file = file_path;
            Result<void> admitted = Coherence::admit_paths(candidates, paths.views);
            if (!admitted)
                paths.refusal = std::move(admitted.error());
            return paths;
        }
    } // namespace Internal::Execution
} // namespace SushiRuntime
