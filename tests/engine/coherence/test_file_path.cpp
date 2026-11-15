/**************************************************************************/
/* test_file_path.cpp                                                     */
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
 * @file test_file_path.cpp
 * @brief Tests that FilePath streams a file range through the staging ring with byte-equal results and host memory bounded by the ring.
 * @see tests/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/coherence/file_path.hpp>
#include <SushiRuntime/memory/device_memory_space.hpp>
#include <SushiRuntime/memory/file_memory_space.hpp>
#include <SushiRuntime/memory/host_memory_space.hpp>

#include "fake_transfer_path.hpp"
#include "transfer_path_doubles.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;
using SushiRuntime::Internal::Memory::DeviceMemorySpace;
using SushiRuntime::Internal::Memory::DeviceMemorySpaceDescription;
using SushiRuntime::Internal::Memory::FileMemorySpace;
using SushiRuntime::Internal::Memory::FileRange;
using SushiRuntime::Internal::Memory::HostMemorySpace;
using SushiRuntime::Internal::Memory::HostMemorySpaceDescription;

namespace
{
    const MemorySpaceID K_HOST(0);
    const MemorySpaceID K_DEVICE(1);
    const MemorySpaceID K_FILE(2);
    constexpr std::size_t K_SLOT_BYTES = 64;
    constexpr std::size_t K_SLOT_COUNT = 2;
    constexpr std::uint64_t K_RANGE_OFFSET = 13;

    /** @brief Returns the byte the test files hold at position. */
    std::uint8_t byte_at(std::uint64_t position) noexcept
    {
        return static_cast<std::uint8_t>((position * 131U) ^ (position >> 8U));
    }

    /** @brief Makes pinned host memory from the heap and keeps the peak of the bytes it holds. */
    class CountingPinnedHostMemory final : public PinnedHostMemory
    {
        public:
            [[nodiscard]] void* allocate(std::size_t bytes) override
            {
                void* const address = std::malloc(bytes);
                const std::lock_guard<std::mutex> lock(mutex_);
                sizes_.push_back({address, bytes});
                held_ += bytes;
                peak_ = std::max(peak_, held_);
                return address;
            }

            void release(void* address) noexcept override
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                for (auto at = sizes_.begin(); at != sizes_.end(); ++at)
                    if (at->first == address)
                    {
                        held_ -= at->second;
                        sizes_.erase(at);
                        break;
                    }
                std::free(address);
            }

            /** @brief Returns the most bytes held at once. */
            [[nodiscard]] std::size_t peak() const
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                return peak_;
            }

        private:
            /// @brief Guards the fields below.
            mutable std::mutex mutex_;

            /// @brief Each live allocation and its size.
            std::vector<std::pair<void*, std::size_t>> sizes_;

            /// @brief Bytes held now.
            std::size_t held_ = 0;

            /// @brief Most bytes held at once.
            std::size_t peak_ = 0;
    };

    /** @brief Holds a temporary file, a host space, a CPU device and a file space over a range of the file. */
    class Small_FilePath : public ::testing::Test
    {
        protected:
            static constexpr std::size_t K_RANGE_BYTES = K_SLOT_BYTES * 7 + 5;

            void SetUp() override
            {
                path_ = (std::filesystem::temp_directory_path() /
                         ("sushiruntime_file_path_" +
                          std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()) + ".bin"))
                            .string();
                std::ofstream out(path_, std::ios::binary);
                for (std::uint64_t position = 0; position < K_RANGE_OFFSET + K_RANGE_BYTES + 3; ++position)
                    out.put(static_cast<char>(byte_at(position)));
                out.close();
                file_.emplace(K_FILE, FileRange{path_, K_RANGE_OFFSET, K_RANGE_BYTES});
                subject_.add(*file_);
            }

            void TearDown() override
            {
                std::error_code ignored;
                std::filesystem::remove(path_, ignored);
            }

            /** @brief Returns the bytes the range holds from its byte begin, bytes long. */
            static std::vector<std::uint8_t> expected(std::size_t begin, std::size_t bytes)
            {
                std::vector<std::uint8_t> data(bytes);
                for (std::size_t index = 0; index < bytes; ++index)
                    data[index] = byte_at(K_RANGE_OFFSET + begin + index);
                return data;
            }

            /** @brief Returns the instance address of the file space plus begin. */
            static const void* in_file(std::size_t begin)
            {
                return static_cast<const unsigned char*>(FileMemorySpace::instance_address()) + begin;
            }

            std::string path_;
            std::optional<FileMemorySpace> file_;
            FixedMemoryProbe probe_;
            HostMemorySpace host_{HostMemorySpaceDescription{K_HOST, 0, 0, {}}, probe_};
            DeviceMemorySpace device_{DeviceMemorySpaceDescription{K_DEVICE, 1, {}}, probe_};
            CPUSpaceQueues cpu_queues_;
            QueuesWithoutSpace queues_{cpu_queues_, K_HOST};
            CountingPinnedHostMemory memory_;
            StagingRing ring_{memory_, StagingRingShape{K_SLOT_BYTES, K_SLOT_COUNT}};
            RecordingTransferObserver observer_;
            FilePath subject_{queues_, ring_, &observer_};
    };

    TEST_F(Small_FilePath, ConnectsAnAddedFileToAnySpaceThatIsNotAFile)
    {
        const FileMemorySpace other(MemorySpaceID(3), FileRange{path_, 0, 1});

        EXPECT_TRUE(subject_.connects(*file_, device_));
        EXPECT_TRUE(subject_.connects(*file_, host_));
        EXPECT_FALSE(subject_.connects(*file_, other));
        EXPECT_FALSE(subject_.connects(other, device_));
        EXPECT_FALSE(subject_.connects(device_, host_));
        EXPECT_EQ(subject_.name(), "file");
    }

    TEST_F(Small_FilePath, StreamsTheRangeOntoADeviceThroughTheRingWithEqualBytes)
    {
        if (!cpu_queues_.available())
            GTEST_SKIP() << "SYCL offers no CPU device.";
        const std::size_t starts[] = {0, 1, K_SLOT_BYTES, 3};
        const std::size_t lengths[] = {K_RANGE_BYTES, K_SLOT_BYTES - 1, K_SLOT_BYTES * 5, K_SLOT_BYTES + 1};
        for (std::size_t index = 0; index < 4; ++index)
        {
            std::vector<std::uint8_t> on_device(lengths[index], 0);

            subject_.copy(TransferRequest{K_FILE, K_DEVICE, in_file(starts[index]), on_device.data(),
                                          lengths[index]}, {}).wait();

            EXPECT_EQ(on_device, expected(starts[index], lengths[index])) << "case " << index;
        }
        EXPECT_EQ(memory_.peak(), K_SLOT_BYTES * K_SLOT_COUNT);
    }

    TEST_F(Small_FilePath, ReadsStraightIntoTheHostWithNoSlot)
    {
        std::vector<std::uint8_t> on_host(K_RANGE_BYTES, 0);

        subject_.copy(TransferRequest{K_FILE, K_HOST, in_file(0), on_host.data(), K_RANGE_BYTES}, {}).wait();

        EXPECT_EQ(on_host, expected(0, K_RANGE_BYTES));
        EXPECT_EQ(memory_.peak(), 0U);
        ASSERT_EQ(observer_.heard().size(), 1U);
        EXPECT_EQ(observer_.heard()[0].path, "file");
        EXPECT_EQ(observer_.heard()[0].bytes, K_RANGE_BYTES);
    }

    TEST_F(Small_FilePath, RefusesARangeThePathDoesNotHoldOrThatRunsPastTheFile)
    {
        std::vector<std::uint8_t> on_host(K_RANGE_BYTES + 64, 0);

        EXPECT_THROW(static_cast<void>(subject_.copy(
                         TransferRequest{MemorySpaceID(9), K_HOST, in_file(0), on_host.data(), 1}, {})),
                     std::runtime_error);
        EXPECT_THROW(static_cast<void>(subject_.copy(
                         TransferRequest{K_FILE, K_HOST, in_file(0), on_host.data(), on_host.size()}, {})),
                     std::runtime_error);
    }

    /** @brief Names the queue of one CUDA GPU for its space and none for any other. */
    class OneDeviceQueues final : public SpaceQueues
    {
        public:
            OneDeviceQueues(sycl::queue& queue, MemorySpaceID space) noexcept : queue_(&queue), space_(space)
            {

            }

            [[nodiscard]] sycl::queue* queue_of(MemorySpaceID space) const override
            {
                return space == space_ ? queue_ : nullptr;
            }

        private:
            /// @brief The GPU's queue.
            sycl::queue* queue_;

            /// @brief The GPU's space.
            MemorySpaceID space_;
    };

    /** @brief Makes pinned host memory on one queue's context and keeps the peak of the bytes it holds. */
    class QueuePinnedHostMemory final : public PinnedHostMemory
    {
        public:
            explicit QueuePinnedHostMemory(sycl::queue& queue) noexcept : queue_(&queue)
            {

            }

            [[nodiscard]] void* allocate(std::size_t bytes) override
            {
                held_ += bytes;
                peak_ = std::max(peak_, held_);
                return sycl::malloc_host(bytes, *queue_);
            }

            void release(void* address) noexcept override
            {
                sycl::free(address, *queue_);
            }

            /** @brief Returns the most bytes allocated. */
            [[nodiscard]] std::size_t peak() const noexcept
            {
                return peak_;
            }

        private:
            /// @brief The queue whose context holds the memory.
            sycl::queue* queue_;

            /// @brief Bytes allocated so far; slots are freed only with the ring.
            std::size_t held_ = 0;

            /// @brief Most bytes allocated.
            std::size_t peak_ = 0;
    };

    /** @brief Returns the first CUDA GPU SYCL offers, or nothing. */
    std::optional<sycl::device> cuda_gpu()
    {
        for (const sycl::device& device : sycl::device::get_devices(sycl::info::device_type::gpu))
            if (device.get_info<sycl::info::device::vendor>().find("NVIDIA") != std::string::npos)
                return device;
        return std::nullopt;
    }

    TEST(Large_FilePath, AFileBackedBufferLargerThanTheDeviceMemoryStreamsOntoTheDevice)
    {
        constexpr std::size_t K_READ_BUFFER_LIMIT = std::size_t{512} * 1024 * 1024;
        constexpr std::size_t K_WINDOW_BYTES = std::size_t{256} * 1024 * 1024;
        constexpr std::size_t K_CHECK_BYTES = 4096;
        const std::optional<sycl::device> gpu = cuda_gpu();
        if (!gpu)
            GTEST_SKIP() << "No CUDA GPU.";
        const std::uint64_t file_bytes =
            gpu->get_info<sycl::info::device::global_mem_size>() + std::uint64_t{1024} * 1024 * 1024;
        const std::filesystem::path folder = std::filesystem::current_path() / "temp";
        std::filesystem::create_directories(folder);
        if (std::filesystem::space(folder).available < file_bytes + K_WINDOW_BYTES)
            GTEST_SKIP() << "Not enough disk for a " << file_bytes << "-byte file.";
        const std::string path = (folder / "sushiruntime_file_path_large.bin").string();
        {
            std::ofstream out(path, std::ios::binary);
            std::vector<char> block(K_WINDOW_BYTES);
            for (std::uint64_t written = 0; written < file_bytes; written += block.size())
            {
                const std::size_t bytes = static_cast<std::size_t>(std::min<std::uint64_t>(block.size(),
                                                                                         file_bytes - written));
                for (std::size_t index = 0; index < bytes; ++index)
                    block[index] = static_cast<char>(byte_at(written + index));
                out.write(block.data(), static_cast<std::streamsize>(bytes));
            }
            ASSERT_TRUE(out.good());
        }

        {
            sycl::queue queue(*gpu);
            const MemorySpaceID device_space(1);
            const MemorySpaceID file_space(2);
            const OneDeviceQueues queues(queue, device_space);
            QueuePinnedHostMemory pinned(queue);
            StagingRing ring(pinned);
            FilePath subject(queues, ring);
            const FileMemorySpace file(file_space, FileRange{path, 0, static_cast<std::size_t>(file_bytes)});
            subject.add(file);
            auto* const window = sycl::malloc_device<std::uint8_t>(K_WINDOW_BYTES, queue);
            ASSERT_NE(window, nullptr);
            std::vector<std::uint8_t> head(K_CHECK_BYTES);
            std::vector<std::uint8_t> tail(K_CHECK_BYTES);
            for (std::uint64_t begin = 0; begin < file_bytes; begin += K_WINDOW_BYTES)
            {
                const std::size_t bytes = static_cast<std::size_t>(
                    std::min<std::uint64_t>(K_WINDOW_BYTES, file_bytes - begin));
                const void* const from =
                    static_cast<const unsigned char*>(FileMemorySpace::instance_address()) + begin;
                subject.copy(TransferRequest{file_space, device_space, from, window, bytes}, {}).wait();
                const std::size_t checked = std::min(K_CHECK_BYTES, bytes);
                queue.memcpy(head.data(), window, checked).wait();
                queue.memcpy(tail.data(), window + bytes - checked, checked).wait();
                for (std::size_t index = 0; index < checked; ++index)
                {
                    ASSERT_EQ(head[index], byte_at(begin + index)) << "at " << begin + index;
                    ASSERT_EQ(tail[index], byte_at(begin + bytes - checked + index));
                }
            }
            sycl::free(window, queue);
            const StagingRingShape shape = ring.shape();
            EXPECT_LE(pinned.peak(), shape.slot_bytes * shape.slot_count);
            EXPECT_LT(pinned.peak(), shape.slot_bytes * shape.slot_count + K_READ_BUFFER_LIMIT);
        }
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
} // namespace
