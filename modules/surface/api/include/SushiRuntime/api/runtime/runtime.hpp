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
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include <sycl/sycl.hpp>

#include <SushiRuntime/context/context_handle.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/placement_class.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/core/version.hpp>
#include <SushiRuntime/machine/node_facts.hpp>
#include <SushiRuntime/placement/placement_rules.hpp>

#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/api/data/state.hpp>
#include <SushiRuntime/api/graph/dynamic_graph.hpp>
#include <SushiRuntime/api/graph/graph.hpp>
#include <SushiRuntime/api/interop/interop.hpp>
#include <SushiRuntime/api/runtime/advanced_controls.hpp>
#include <SushiRuntime/api/runtime/placement_scope.hpp>
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
             * @return The runtime, or an InvalidArgument, Unsupported, DeviceLost or
             *         AllocationFailed error.
             * @pre The loaded library is of the release of these headers; version_matches()
             *      tells.
             * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
             */
            [[nodiscard]] static Result<Runtime> create(const RuntimeConfig& config = {})
            {
                require_matching_version();
                Result<std::shared_ptr<NodeFacts>> facts = create_topology(config);
                if (!facts)
                    return make_error(std::move(facts.error()));
                Result<ContextHandle> context = create_context(config);
                if (!context)
                    return make_error(std::move(context.error()));
                Result<void> listed = list_devices(*context, **facts);
                if (!listed)
                    return make_error(std::move(listed.error()));
                return Runtime(std::move(*context), std::move(*facts));
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
            [[nodiscard]] DynamicGraph dynamic_graph() const
            {
                return DynamicGraph(context_);
            }

            /**
             * @brief Allocates a typed buffer of @p count elements on one device.
             * @param alignment Byte alignment of the address; 0 asks for the device default.
             * @return The buffer, or an AllocationFailed error when the device returns nothing.
             * @pre device names a device of this runtime, the byte size of count fits
             *      std::size_t, and alignment is 0 or a power of two.
             * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
             */
            template <typename T>
            [[nodiscard]] Result<Buffer<T>> buffer(std::size_t count,
                                                   DeviceIndex device = DeviceIndex{0},
                                                   Residency residency = Residency::Shared,
                                                   std::size_t alignment = 0) const
            {
                return Buffer<T>::allocate("Runtime::buffer", context_, count, device, residency,
                                           alignment);
            }

            /**
             * @brief Allocates a double-buffered field of @p count elements on one device.
             * @param alignment Byte alignment of both fields; 0 asks for the device default.
             * @return The state, or an AllocationFailed error when the device returns nothing.
             * @pre device names a device of this runtime, the byte size of count fits
             *      std::size_t, and alignment is 0 or a power of two.
             * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
             */
            template <typename T>
            [[nodiscard]] Result<State<T>> state(std::size_t count,
                                                 DeviceIndex device = DeviceIndex{0},
                                                 Residency residency = Residency::Shared,
                                                 std::size_t alignment = 0) const
            {
                return State<T>::allocate("Runtime::state", context_, count, device, residency,
                                          alignment);
            }

            /**
             * @brief Starts or stops the background thermal rebalancer.
             * @pre No run is in flight.
             * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
             */
            SUSHIRUNTIME_API const Runtime& set_rebalancer_enabled(bool enabled) const;

            /**
             * @brief Blocks until every device queue of this runtime has finished its work.
             * @return Nothing, or a KernelFailed error when a queue reports a failed command.
             * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
             */
            [[nodiscard]] SUSHIRUNTIME_API Result<void> wait() const;

            /** @brief Returns the view of this runtime's devices, queues and counters. */
            [[nodiscard]] AdvancedControls advanced() const noexcept
            {
                return AdvancedControls(context_);
            }

            /**
             * @brief Returns the facts of the machine this runtime runs on.
             * @return The facts built by create(); valid while this Runtime is not moved from.
             * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
             */
            [[nodiscard]] SUSHIRUNTIME_API const NodeFacts& topology() const;

            /**
             * @brief Returns the scope that names a placement class and a key, bound to this
             *        runtime's rules and devices.
             * @param name Dotted name of the class, such as "sushiai.replica".
             * @param key  Key of the scope; 0 is a key like any other.
             * @pre name is not empty, does not start or end with '.' and holds no "..".
             * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
             */
            [[nodiscard]] SUSHIRUNTIME_API PlacementScope
            placement_scope(std::string_view name,
                            std::optional<std::uint64_t> key = std::nullopt) const;

            /**
             * @brief Returns the scope that names a class built elsewhere, such as "x"_class.
             * @param key Key of the scope; 0 is a key like any other.
             * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
             */
            [[nodiscard]] PlacementScope
            placement_scope(PlacementClass placement_class,
                            std::optional<std::uint64_t> key = std::nullopt) const noexcept
            {
                return PlacementScope(placement_class, key, context_, topology_);
            }

            /**
             * @brief Installs the rules that place the nodes of every graph of this runtime.
             *
             * A graph reads them at its next compile; a plan compiled earlier goes stale.
             * @return Nothing, or ErrorCode::InvalidState while a run of this runtime is in
             *         flight; wait for it and call again.
             * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
             */
            [[nodiscard]] SUSHIRUNTIME_API Result<void>
            set_placement_rules(PlacementRules rules) const;

            /**
             * @brief Removes the placement rules, so the next compile of a graph places by none.
             * @return Nothing, or ErrorCode::InvalidState while a run of this runtime is in flight.
             * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
             */
            [[nodiscard]] SUSHIRUNTIME_API Result<void> clear_placement_rules() const;

            /** @brief Returns the facade that exchanges this runtime's memory with Vulkan. */
            [[nodiscard]] Interop interop() const noexcept { return Interop(context_); }

        private:
            // The library reads the handle of a runtime through this one seam.
            friend struct Detail::ContextHandleAccess;

            /**
             * @brief Builds the facts of the machine except its devices, and checks the labels.
             * @return The facts, or an InvalidArgument error naming the first label that breaks
             *         a rule, or an Unsupported error when the host name cannot be read.
             */
            [[nodiscard]] SUSHIRUNTIME_API static Result<std::shared_ptr<NodeFacts>>
            create_topology(const RuntimeConfig& config);

            /**
             * @brief Appends one DeviceFacts per device of a context to facts.
             * @return Nothing, or an AllocationFailed error.
             */
            [[nodiscard]] SUSHIRUNTIME_API static Result<void>
            list_devices(const ContextHandle& context, NodeFacts& facts);

            /**
             * @brief Builds the context of a runtime on the devices and settings of a config.
             * @return The handle that shares the context, or why none was built.
             */
            [[nodiscard]] SUSHIRUNTIME_API static Result<ContextHandle>
            create_context(const RuntimeConfig& config);

            /**
             * @brief Takes ownership of a built context and the facts of its machine.
             * @param context Must not be empty.
             */
            Runtime(ContextHandle context, std::shared_ptr<const NodeFacts> facts) noexcept
                : context_(std::move(context)), topology_(std::move(facts))
            {
            }

            /** @brief The context shared with every handle; empty only after a move. */
            ContextHandle context_;

            /** @brief The facts of the machine, built once by create() and shared with scopes. */
            std::shared_ptr<const NodeFacts> topology_;
    };
} // namespace SushiRuntime
