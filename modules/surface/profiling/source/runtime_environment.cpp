/**************************************************************************/
/* runtime_environment.cpp                                                */
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
 * @file runtime_environment.cpp
 * @brief Implements RuntimeEnvironment, which states the runtime's machine and build as facts.
 * @author Mustafa Garip
 * @see modules/surface/profiling/README.md
 */

#include <SushiRuntime/profiling/runtime_environment.hpp>

#include <cstddef>
#include <sstream>
#include <string>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiProfiler/environment/record_facts.hpp>

#include <SushiRuntime/core/version.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/machine/device_description.hpp>
#include <SushiRuntime/machine/node_facts.hpp>
#include <SushiRuntime/machine/worker_pool.hpp>
#include <SushiRuntime/profile/profile_gate.hpp>

namespace SushiRuntime
{
    namespace Internal::Profiling
    {
        namespace
        {
            /** @brief Returns the spelling of a device class. */
            const char* kind_name(DeviceKind kind) noexcept
            {
                switch (kind)
                {
                    case DeviceKind::CPU:
                        return "cpu";
                    case DeviceKind::GPU:
                        return "gpu";
                    case DeviceKind::Accelerator:
                        return "accelerator";
                    case DeviceKind::Other:
                        break;
                }
                return "other";
            }

            /** @brief Returns the spelling of a SYCL backend. */
            std::string backend_name(sycl::backend backend)
            {
                std::ostringstream text;
                text << backend;
                return text.str();
            }

            /** @brief Returns the driver version the device reports, or "unknown" when it does not answer. */
            std::string driver_of(Execution::RuntimeContext& context, std::size_t device)
            {
                try
                {
                    return context.devices().queue(device).get_device()
                        .get_info<sycl::info::device::driver_version>();
                }
                catch (const sycl::exception&)
                {
                    return "unknown";
                }
            }

            /** @brief Returns how this library was compiled: release when assertions are off. */
            const char* build_type() noexcept
            {
#if defined(NDEBUG)
                return "release";
#else
                return "debug";
#endif
            }

            /** @brief Returns the sanitizer this library was compiled with, or "none". */
            const char* sanitizer() noexcept
            {
#if defined(__SANITIZE_ADDRESS__)
                return "address";
#elif defined(__SANITIZE_THREAD__)
                return "thread";
#else
                return "none";
#endif
            }

            /** @brief Returns the SYCL compiler version this library was compiled with, or "unknown". */
            std::string sycl_toolchain()
            {
#if defined(__SYCL_COMPILER_VERSION)
                return std::to_string(__SYCL_COMPILER_VERSION);
#else
                return "unknown";
#endif
            }
        } // namespace

        RuntimeEnvironment::RuntimeEnvironment(
            const std::vector<std::shared_ptr<Execution::RuntimeContext>>& runtimes) noexcept
            : runtimes_(runtimes)
        {
        }

        void RuntimeEnvironment::contribute(SushiProfiler::RecordFacts& facts) const
        {
            using SushiProfiler::set_fact;

            set_fact(facts.environment, "sushiruntime.version", SUSHIRUNTIME_VERSION_STRING);
            set_fact(facts.build, "sushiruntime.build_type", build_type());
            set_fact(facts.build, "sushiruntime.profile_level", std::to_string(SR_PROFILE_LEVEL));
            set_fact(facts.build, "sushiruntime.sanitizer", sanitizer());
            set_fact(facts.build, "sushiruntime.sycl_toolchain", sycl_toolchain());

            for (const std::shared_ptr<Execution::RuntimeContext>& held : runtimes_)
            {
                Execution::RuntimeContext& context = *held;
                const NodeFacts* node = context.machine_facts();
                const std::string prefix =
                    "sushiruntime." + (node != nullptr ? node->name : std::string("node")) + ".";

                const std::vector<DeviceDescription>& devices = context.devices().devices();
                set_fact(facts.environment, prefix + "devices", std::to_string(devices.size()));
                for (const DeviceDescription& device : devices)
                {
                    const std::string key = prefix + "device." + std::to_string(device.index) + ".";
                    set_fact(facts.environment, key + "name", device.name);
                    set_fact(facts.environment, key + "kind", kind_name(device.kind));
                    set_fact(facts.environment, key + "backend", backend_name(device.backend));
                    set_fact(facts.environment, key + "driver", driver_of(context, device.index));
                    set_fact(facts.environment, key + "memory_bytes", std::to_string(device.memory_bytes));
                    set_fact(facts.environment, key + "compute_units", std::to_string(device.compute_units));
                    set_fact(facts.environment, key + "numa_node", std::to_string(device.numa_node));
                }

                const WorkerPool pool = context.execution_engine().worker_pool();
                set_fact(facts.environment, prefix + "workers", std::to_string(pool.count));
                set_fact(facts.environment, prefix + "rebalancer",
                         context.execution_engine().rebalancer_enabled() ? "on" : "off");
            }
        }
    } // namespace Profiling
} // namespace SushiRuntime
