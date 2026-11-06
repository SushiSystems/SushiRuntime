/**************************************************************************/
/* slot_buffers.hpp                                                       */
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
 * @file slot_buffers.hpp
 * @brief Holds one device-resident, one shared and one host-resident counter buffer, the three residencies a late-bound slot is checked against.
 * @see tests/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstdint>

#include <SushiRuntime/sushiruntime.hpp>

namespace TestSupport
{

/**
 * @brief Holds one counter buffer of each residency, so a test can name a slot a late-bound word refuses or accepts.
 */
struct SlotBuffers
{
    SushiRuntime::Buffer<std::uint32_t> device; /**< One element in device memory, which the host cannot read. */
    SushiRuntime::Buffer<std::uint32_t> shared; /**< One element in shared memory, which a run may move. */
    SushiRuntime::Buffer<std::uint32_t> host;   /**< Four elements in host memory, the only residency a slot may have. */
};

/**
 * @brief Allocates a SlotBuffers on device 0 of @p runtime.
 * @param runtime The runtime to allocate from.
 * @return One buffer of each residency.
 */
[[nodiscard]] inline SlotBuffers make_slot_buffers(SushiRuntime::Runtime& runtime)
{
    using SushiRuntime::DeviceIndex;
    using SushiRuntime::Residency;
    return SlotBuffers{
        runtime.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Device).value(),
        runtime.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Shared).value(),
        runtime.buffer<std::uint32_t>(4, DeviceIndex{0}, Residency::Host).value()};
}

} // namespace TestSupport
