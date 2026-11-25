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
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <SushiRuntime/device/queue_flavour.hpp>
#include <SushiRuntime/task/handle_location.hpp>
#include <SushiRuntime/transfer/guarded_transfer_path.hpp>
#include <SushiRuntime/transfer/host_staged_path.hpp>
#include <SushiRuntime/transfer/peer_access.hpp>
#include <SushiRuntime/transfer/peer_path.hpp>
#include <SushiRuntime/transfer/pinned_host_memory.hpp>
#include <SushiRuntime/transfer/queue_copy_submitter.hpp>
#include <SushiRuntime/transfer/same_context_path.hpp>
#include <SushiRuntime/transfer/space_queues.hpp>
#include <SushiRuntime/transfer/staging_rings.hpp>

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

            /** @brief Names the profiling queue of each device of a set by the device's space. */
            class DeviceSpaceQueues final : public Coherence::SpaceQueues
            {
                public:
                    explicit DeviceSpaceQueues(Device::DeviceQueueSet& devices) noexcept : devices_(&devices)
                    {

                    }

                    [[nodiscard]] sycl::queue* queue_of(MemorySpaceID space) const override
                    {
                        const std::optional<std::size_t> device = device_in(*devices_, space);
                        // A copy is a planned node's device work, so a profiled run reads its timestamps.
                        return device ? &devices_->queue(*device, Device::QueueFlavour{false, true}) : nullptr;
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
                        static_cast<void>(devices_);
                        static_cast<void>(reader);
                        static_cast<void>(owner);
                        return false;
#endif
                    }

                private:
                    /// @brief The devices whose peers are asked.
                    Device::DeviceQueueSet* devices_;
            };

            /** @brief Makes pinned host memory in the context of one queue. */
            class QueuePinnedHostMemory final : public Coherence::PinnedHostMemory
            {
                public:
                    explicit QueuePinnedHostMemory(const sycl::queue& queue) : queue_(queue)
                    {

                    }

                    [[nodiscard]] void* allocate(std::size_t bytes) override
                    {
                        try
                        {
                            return sycl::malloc_host(bytes, queue_);
                        }
                        catch (const sycl::exception&)
                        {
                            return nullptr;
                        }
                    }

                    void release(void* address) noexcept override
                    {
                        sycl::free(address, queue_);
                    }

                private:
                    /// @brief The queue whose context holds the memory.
                    sycl::queue queue_;
            };
        } // namespace

        /** @brief Holds the queues and the access the paths of a RuntimeTransferPaths refer to. */
        struct RuntimeTransferPaths::Support
        {
            /** @brief Builds one staging ring per context of the devices, in device order. */
            explicit Support(Device::DeviceQueueSet& devices) : queues(devices), access(devices)
            {
                for (std::size_t device = 0; device < devices.count(); ++device)
                {
                    const sycl::queue& queue = devices.queue(device);
                    if (rings.ring_of(queue.get_context()) != nullptr)
                        continue;
                    pinned.push_back(std::make_unique<QueuePinnedHostMemory>(queue));
                    rings.add(queue.get_context(), *pinned.back());
                }
            }

            DeviceSpaceQueues queues;                                  /**< Names the queue of each space. */
            DevicePeerAccess access;                                   /**< Grants peer access between devices. */
            std::vector<std::unique_ptr<QueuePinnedHostMemory>> pinned; /**< Makes the slots of each context's ring. */
            Coherence::StagingRings rings;                             /**< One ring per context; freed before pinned. */
            Coherence::QueueCopySubmitter submitter;                   /**< Submits each leg of a staged copy. */
        };

        RuntimeTransferPaths::RuntimeTransferPaths() = default;
        RuntimeTransferPaths::RuntimeTransferPaths(RuntimeTransferPaths&&) noexcept = default;
        RuntimeTransferPaths& RuntimeTransferPaths::operator=(RuntimeTransferPaths&&) noexcept = default;
        RuntimeTransferPaths::~RuntimeTransferPaths() = default;

        RuntimeTransferPaths build_transfer_paths(Device::DeviceQueueSet& devices,
                                                  SushiRuntime::TransferObserver* observer,
                                                  Memory::DeviceAccessGate* gate)
        {
            RuntimeTransferPaths paths;
            paths.support = std::make_unique<RuntimeTransferPaths::Support>(devices);
            auto file = std::make_unique<Coherence::FilePath>(paths.support->queues, paths.support->rings, observer);
            Coherence::FilePath* const file_path = file.get();

            std::vector<std::unique_ptr<Coherence::TransferPath>> built;
            built.push_back(std::make_unique<Coherence::SameContextPath>(paths.support->queues, observer));
            built.push_back(
                std::make_unique<Coherence::PeerPath>(paths.support->queues, paths.support->access, observer));
            built.push_back(
                std::make_unique<Coherence::HostStagedPath>(paths.support->queues, paths.support->rings,
                                                           paths.support->submitter, observer));
            built.push_back(std::move(file));

            std::vector<Coherence::TransferPath*> candidates;
            for (std::unique_ptr<Coherence::TransferPath>& path : built)
            {
                // A guarded path keeps the file path it wraps, so paths.file still names it.
                if (gate != nullptr)
                    path = Coherence::make_guarded_transfer_path(std::move(path), *gate);
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
