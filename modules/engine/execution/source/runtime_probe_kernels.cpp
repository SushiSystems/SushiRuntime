/**************************************************************************/
/* runtime_probe_kernels.cpp                                              */
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
 * @file runtime_probe_kernels.cpp
 * @brief Implements runtime_probe_kernels().
 * @see modules/engine/execution/README.md
 * @author Mustafa Garip
 */

#include "runtime_probe_kernels.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <sycl/sycl.hpp>

#include <SushiRuntime/core/error.hpp>

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        namespace
        {
            /** @brief Holds the elements the streaming copy moves: 16 MiB of floats. */
            constexpr std::size_t K_STREAM_ELEMENTS = std::size_t{1} << 22;

            /** @brief Holds the work items of the multiply-add kernel. */
            constexpr std::size_t K_FMA_ITEMS = std::size_t{1} << 16;

            /** @brief Holds the multiply-adds each work item does in a dependent chain. */
            constexpr std::uint32_t K_FMA_CHAIN = 1024;

            /** @brief Names the streaming copy kernel. */
            class RuntimeProbeStreamCopy;

            /** @brief Names the multiply-add kernel. */
            class RuntimeProbeMultiplyAdd;

            /** @brief Frees a device array when it leaves scope. */
            struct DeviceFree
            {
                sycl::queue* queue = nullptr; /**< Queue whose context owns the array. */

                /** @brief Frees address on the queue's context. */
                void operator()(float* address) const
                {
                    sycl::free(address, *queue);
                }
            };

            /** @brief Owns a device array of floats. */
            using DeviceArray = std::unique_ptr<float, DeviceFree>;

            /** @brief Returns a device array of count floats, empty when the device refused it. */
            [[nodiscard]] DeviceArray device_array(sycl::queue& queue, std::size_t count)
            {
                return DeviceArray(sycl::malloc_device<float>(count, queue), DeviceFree{&queue});
            }

            /** @brief Returns the refusal of a probe allocation. */
            [[nodiscard]] Error refused(std::size_t count)
            {
                return Error{ErrorCode::AllocationFailed,
                             "the runtime's probe kernel cannot allocate " + std::to_string(count) +
                                 " floats on the device."};
            }

            /** @brief Streams one array into another and returns the elements copied. */
            [[nodiscard]] Result<std::uint64_t> stream_copy(sycl::queue& queue)
            {
                DeviceArray from = device_array(queue, K_STREAM_ELEMENTS);
                DeviceArray to = device_array(queue, K_STREAM_ELEMENTS);
                if (!from || !to)
                    return make_error(refused(2 * K_STREAM_ELEMENTS));
                queue.fill(from.get(), 1.0F, K_STREAM_ELEMENTS).wait();

                const float* source = from.get();
                float* destination = to.get();
                queue.parallel_for<RuntimeProbeStreamCopy>(sycl::range<1>(K_STREAM_ELEMENTS),
                                                           [=](sycl::id<1> index)
                                                           { destination[index] = source[index]; })
                    .wait();
                return static_cast<std::uint64_t>(K_STREAM_ELEMENTS);
            }

            /** @brief Runs a fixed chain of multiply-adds per work item and returns the multiply-adds done. */
            [[nodiscard]] Result<std::uint64_t> multiply_add(sycl::queue& queue)
            {
                DeviceArray out = device_array(queue, K_FMA_ITEMS);
                if (!out)
                    return make_error(refused(K_FMA_ITEMS));

                float* destination = out.get();
                queue.parallel_for<RuntimeProbeMultiplyAdd>(sycl::range<1>(K_FMA_ITEMS),
                                                            [=](sycl::id<1> index)
                                                            {
                                                                float value = static_cast<float>(index[0]) * 1.0e-6F;
                                                                for (std::uint32_t step = 0; step < K_FMA_CHAIN; ++step)
                                                                    value = sycl::fma(value, 0.999F, 0.001F);
                                                                destination[index] = value;
                                                            })
                    .wait();
                return static_cast<std::uint64_t>(K_FMA_ITEMS) * K_FMA_CHAIN;
            }
        } // namespace

        std::map<OperationKind, Coherence::ProbeKernel> runtime_probe_kernels()
        {
            std::map<OperationKind, Coherence::ProbeKernel> kernels;
            kernels.emplace(OperationKinds::K_GENERIC, &multiply_add);
            kernels.emplace(OperationKinds::K_MEMORY_BOUND, &stream_copy);
            return kernels;
        }
    } // namespace Internal::Execution
} // namespace SushiRuntime
