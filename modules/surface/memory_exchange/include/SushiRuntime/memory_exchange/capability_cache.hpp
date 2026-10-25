/**************************************************************************/
/* capability_cache.hpp                                                   */
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
 * @file capability_cache.hpp
 * @brief Declares the cache that asks each SYCL device for its interop capabilities once.
 * @author Sushi (mustafagarip@sushisystems.io)
 */

#pragma once

#include <functional>
#include <mutex>
#include <utility>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/interop/interop_capabilities.hpp>

namespace SushiRuntime::Internal::MemoryExchange
{
    /**
     * @brief Asks @p device what it offers a pairing, before its context's GPU check.
     * @throws What SYCL throws.
     */
    [[nodiscard]] SUSHIRUNTIME_API InteropCapabilities probe_device_capabilities(
        const sycl::device& device);

    /**
     * @brief Asks each SYCL device for its interop capabilities once and keeps the answer.
     *
     * Safe to call from several threads; see modules/surface/memory_exchange/README.md.
     */
    class CapabilityCache final
    {
        public:
            /// @brief Names the function that asks one device for its capabilities.
            using Probe = std::function<InteropCapabilities(const sycl::device&)>;

            /// @brief Builds a cache that asks devices through probe_device_capabilities().
            CapabilityCache() : CapabilityCache(probe_device_capabilities)
            {
            }

            /// @brief Binds the cache to the probe it calls on a device it has not seen.
            explicit CapabilityCache(Probe probe) : probe_(std::move(probe))
            {
            }

            CapabilityCache(const CapabilityCache&) = delete;
            CapabilityCache& operator=(const CapabilityCache&) = delete;
            CapabilityCache(CapabilityCache&&) = delete;
            CapabilityCache& operator=(CapabilityCache&&) = delete;
            ~CapabilityCache() = default;

            /**
             * @brief Returns the capabilities of @p device, probing it only on its first query.
             * @throws What the probe throws; nothing is kept for the device then.
             */
            [[nodiscard]] InteropCapabilities get(const sycl::device& device)
            {
                // The lock is held across the probe, so two first queries probe once.
                const std::lock_guard<std::mutex> lock(mutex_);
                for (const auto& entry : entries_)
                {
                    if (entry.first == device)
                        return entry.second;
                }
                InteropCapabilities capabilities = probe_(device);
                entries_.emplace_back(device, capabilities);
                return capabilities;
            }

        private:
            /// @brief Holds the function that asks a device for its capabilities.
            Probe probe_;
            /// @brief Guards entries_.
            std::mutex mutex_;
            /// @brief Holds one answer per device probed, in probe order.
            std::vector<std::pair<sycl::device, InteropCapabilities>> entries_;
    };
} // namespace SushiRuntime::Internal::MemoryExchange
