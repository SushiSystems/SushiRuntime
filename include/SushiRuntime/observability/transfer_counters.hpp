/**************************************************************************/
/* transfer_counters.hpp                                                  */
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
 * @file transfer_counters.hpp
 * @brief Declares TransferCounters, the byte counters for explicit host and device transfers.
 * @author Mustafa Garip
 * @see include/SushiRuntime/observability/README.md
 */

#pragma once

#include <atomic>
#include <cstddef>

namespace SushiRuntime
{
    namespace Observability
    {
        /**
         * @brief Counts the bytes that explicit transfers copy between host and device.
         * @see include/SushiRuntime/observability/README.md
         */
        class TransferCounters final
        {
            public:
                /**
                 * @brief Records bytes copied from device to host by an explicit transfer.
                 * @param bytes Number of bytes copied.
                 */
                void add_device_to_host(std::size_t bytes) noexcept
                {
                    device_to_host_bytes_.fetch_add(bytes, std::memory_order_relaxed);
                }

                /**
                 * @brief Records bytes copied from host to device by an explicit transfer.
                 * @param bytes Number of bytes copied.
                 */
                void add_host_to_device(std::size_t bytes) noexcept
                {
                    host_to_device_bytes_.fetch_add(bytes, std::memory_order_relaxed);
                }

                /**
                 * @brief Returns cumulative device-to-host bytes from explicit transfers.
                 * @return The sum of every add_device_to_host() call so far.
                 */
                [[nodiscard]] std::size_t device_to_host() const noexcept
                {
                    return device_to_host_bytes_.load(std::memory_order_relaxed);
                }

                /**
                 * @brief Returns cumulative host-to-device bytes from explicit transfers.
                 * @return The sum of every add_host_to_device() call so far.
                 */
                [[nodiscard]] std::size_t host_to_device() const noexcept
                {
                    return host_to_device_bytes_.load(std::memory_order_relaxed);
                }

            private:
                /** @brief Cumulative bytes copied from device to host. */
                std::atomic<std::size_t> device_to_host_bytes_{0};

                /** @brief Cumulative bytes copied from host to device. */
                std::atomic<std::size_t> host_to_device_bytes_{0};
        };
    } // namespace Observability
} // namespace SushiRuntime
