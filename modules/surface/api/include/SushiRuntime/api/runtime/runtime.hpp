/**************************************************************************/
/* runtime.hpp                                                            */
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
 * @file runtime.hpp
 * @brief Declares Runtime, the entry point that builds a runtime and hands out its handles.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
 */

#pragma once

#include <cstddef>
#include <memory>
#include <utility>

#include <SushiRuntime/core/version.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/execution/runtime_settings.hpp>

#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/api/data/state.hpp>
#include <SushiRuntime/api/graph/dynamic_graph.hpp>
#include <SushiRuntime/api/graph/graph.hpp>
#include <SushiRuntime/api/interop/interop.hpp>
#include <SushiRuntime/api/runtime/advanced_controls.hpp>
#include <SushiRuntime/api/runtime/runtime_config.hpp>

namespace SushiRuntime
{
    /**
     * @brief Builds a runtime and hands out the graphs, buffers and views that use it.
     *
     * The Runtime and every handle it hands out share ownership of one context, which is
     * destroyed with the last of them. A Runtime moves and does not copy; a moved-from
     * Runtime accepts assignment and destruction only.
     * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
     */
    class Runtime
    {
        public:
            /**
             * @brief Creates a runtime on the devices and with the settings of one struct.
             * @throws SushiException With Errc::unsupported when the loaded library
             *         is of another release than these headers, or when no device passes
             *         config.devices.
             */
            [[nodiscard]] static Runtime create(const RuntimeConfig& config = {})
            {
                require_matching_version();
                return Runtime(std::make_shared<Internal::Execution::RuntimeContext>(
                    config.devices, nullptr, nullptr, settings_of(config)));
            }

            Runtime(Runtime&&) noexcept = default;
            Runtime& operator=(Runtime&&) noexcept = default;
            Runtime(const Runtime&) = delete;
            Runtime& operator=(const Runtime&) = delete;
            ~Runtime() = default;

            /** @brief Returns a new, empty graph that runs on this runtime. */
            [[nodiscard]] Graph graph() const { return Graph(context_); }

            /**
             * @brief Returns a new, empty graph whose work is grouped into regions.
             * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
             */
            [[nodiscard]] DynamicGraph dynamic_graph() const { return DynamicGraph(context_); }

            /**
             * @brief Allocates a typed buffer of @p count elements on one device.
             * @throws SushiException With Errc::invalid_access when @p device names no
             *         device of this runtime; the message names Runtime::buffer, the index,
             *         the device count and advanced().devices().
             * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
             */
            template <typename T>
            [[nodiscard]] Buffer<T> buffer(std::size_t count,
                                           DeviceIndex device = DeviceIndex{0},
                                           Residency residency = Residency::Shared) const
            {
                return Buffer<T>("Runtime::buffer", context_, count, device, residency);
            }

            /**
             * @brief Allocates a double-buffered field of @p count elements on one device.
             * @throws SushiException With Errc::invalid_access when @p device names no
             *         device of this runtime; the message names Runtime::state, the index,
             *         the device count and advanced().devices().
             * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
             */
            template <typename T>
            [[nodiscard]] State<T> state(std::size_t count,
                                         DeviceIndex device = DeviceIndex{0},
                                         Residency residency = Residency::Shared) const
            {
                return State<T>("Runtime::state", context_, count, device, residency);
            }

            /**
             * @brief Starts or stops the background thermal rebalancer.
             * @pre No run is in flight.
             * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
             */
            const Runtime& set_rebalancer_enabled(bool enabled) const
            {
                context_->execution_engine().set_rebalancer_enabled(enabled);
                return *this;
            }

            /** @brief Returns the read-only view of this runtime's devices and counters. */
            [[nodiscard]] AdvancedControls advanced() const noexcept
            {
                return AdvancedControls(context_);
            }

            /** @brief Returns the facade that exchanges this runtime's memory with Vulkan. */
            [[nodiscard]] Interop interop() const noexcept { return Interop(context_); }

            /**
             * @brief Returns the runtime context, a type outside the stable surface.
             * @return A reference valid while a handle of this runtime lives.
             * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
             */
            [[nodiscard]] Internal::Execution::RuntimeContext& unsafe_context() const noexcept
            {
                return *context_;
            }

        private:
            /** @brief Returns the settings the context takes from a RuntimeConfig. */
            [[nodiscard]] static Internal::Execution::RuntimeSettings
            settings_of(const RuntimeConfig& config) noexcept
            {
                Internal::Execution::RuntimeSettings settings;
                settings.profiling = config.profiling;
                settings.worker_count = config.worker_count;
                settings.first_core = config.first_core;
                settings.pinning = config.pinning;
                settings.rebalancer = config.rebalancer;
                settings.force_scheduler = config.force_scheduler;
                return settings;
            }

            /**
             * @brief Takes ownership of a built context.
             * @param context Must not be null.
             */
            explicit Runtime(std::shared_ptr<Internal::Execution::RuntimeContext> context) noexcept
                : context_(std::move(context))
            {
            }

            /// @brief Context shared with every handle; null only after a move.
            std::shared_ptr<Internal::Execution::RuntimeContext> context_;
    };



} // namespace SushiRuntime
