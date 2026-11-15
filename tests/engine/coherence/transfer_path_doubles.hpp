/**************************************************************************/
/* transfer_path_doubles.hpp                                              */
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
 * @file transfer_path_doubles.hpp
 * @brief Declares the test doubles of the same-context and peer path tests.
 * @see tests/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/coherence/peer_access.hpp>
#include <SushiRuntime/coherence/pinned_host_memory.hpp>
#include <SushiRuntime/coherence/space_queues.hpp>
#include <SushiRuntime/coherence/transfer_observer.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Answers the one queue it holds for every space; the queue is a SYCL CPU queue. */
        class CPUSpaceQueues final : public SpaceQueues
        {
            public:
                /** @brief Builds the queue; leaves it empty when SYCL offers no CPU device. */
                CPUSpaceQueues()
                {
                    try
                    {
                        queue_.emplace(sycl::cpu_selector_v);
                    }
                    catch (const sycl::exception&)
                    {
                        queue_.reset();
                    }
                }

                /** @brief Returns whether SYCL offered a CPU device. */
                [[nodiscard]] bool available() const noexcept
                {
                    return queue_.has_value();
                }

                [[nodiscard]] sycl::queue* queue_of(MemorySpaceID) const override
                {
                    return queue_ ? const_cast<sycl::queue*>(&*queue_) : nullptr;
                }

            private:
                /// @brief The queue every space copies on.
                std::optional<sycl::queue> queue_;
        };

        /** @brief Answers the queue of another SpaceQueues for every space but one, which has no queue. */
        class QueuesWithoutSpace final : public SpaceQueues
        {
            public:
                /**
                 * @param inner Answers for every other space; must outlive this object.
                 * @param without The space that gets no queue.
                 */
                QueuesWithoutSpace(const SpaceQueues& inner, MemorySpaceID without) noexcept
                    : inner_(&inner), without_(without)
                {

                }

                [[nodiscard]] sycl::queue* queue_of(MemorySpaceID space) const override
                {
                    return space == without_ ? nullptr : inner_->queue_of(space);
                }

            private:
                /// @brief Answers for every other space.
                const SpaceQueues* inner_;

                /// @brief The space with no queue.
                MemorySpaceID without_;
        };

        /** @brief Makes pinned host memory from the heap and counts the calls. */
        class HeapPinnedHostMemory final : public PinnedHostMemory
        {
            public:
                [[nodiscard]] void* allocate(std::size_t bytes) override
                {
                    ++allocated_;
                    return std::malloc(bytes);
                }

                void release(void* address) noexcept override
                {
                    ++released_;
                    std::free(address);
                }

                /** @brief Returns the allocations made so far. */
                [[nodiscard]] int allocated() const noexcept
                {
                    return allocated_;
                }

                /** @brief Returns the releases made so far. */
                [[nodiscard]] int released() const noexcept
                {
                    return released_;
                }

            private:
                /// @brief Allocations so far.
                int allocated_ = 0;

                /// @brief Releases so far.
                int released_ = 0;
        };

        /** @brief Answers one fixed reply to every request and counts the requests. */
        class FixedPeerAccess final : public PeerAccess
        {
            public:
                explicit FixedPeerAccess(bool granted) noexcept : granted_(granted)
                {

                }

                [[nodiscard]] bool grant(MemorySpaceID, MemorySpaceID) override
                {
                    ++asked_;
                    return granted_;
                }

                /** @brief Returns how many times access was asked for. */
                [[nodiscard]] int asked() const noexcept
                {
                    return asked_;
                }

            private:
                /// @brief The reply to every request.
                bool granted_;

                /// @brief Requests so far.
                int asked_ = 0;
        };

        /** @brief Keeps every record it hears, in the order it hears them. */
        class RecordingTransferObserver final : public TransferObserver
        {
            public:
                /** @brief One heard copy, with the path name kept as text. */
                struct Heard
                {
                    std::string path;          /**< Name of the path. */
                    MemorySpaceID source;      /**< Space read from. */
                    MemorySpaceID destination; /**< Space written to. */
                    std::size_t bytes = 0;     /**< Bytes copied. */
                };

                void copied(const TransferRecord& record) override
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    heard_.push_back(Heard{std::string(record.path), record.source, record.destination, record.bytes});
                }

                /** @brief Returns the copies heard so far. */
                [[nodiscard]] std::vector<Heard> heard() const
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    return heard_;
                }

            private:
                /// @brief Guards heard_.
                mutable std::mutex mutex_;

                /// @brief Copies heard so far.
                std::vector<Heard> heard_;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
