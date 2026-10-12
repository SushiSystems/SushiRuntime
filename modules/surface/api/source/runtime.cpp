/**************************************************************************/
/* runtime.cpp                                                            */
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
 * @file runtime.cpp
 * @brief Implements the members of Runtime that reach the context.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
 */

#include <SushiRuntime/api/runtime/runtime.hpp>

#include <memory>
#include <utility>

#include <SushiRuntime/context_access/context_access.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/execution/runtime_settings.hpp>

namespace SushiRuntime
{
    namespace
    {
        /** @brief Returns the settings the context takes from a RuntimeConfig. */
        [[nodiscard]] Internal::Execution::RuntimeSettings
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
    } // namespace

    Result<ContextHandle> Runtime::create_context(const RuntimeConfig& config)
    {
        Result<std::shared_ptr<Internal::Execution::RuntimeContext>> context =
            Internal::Execution::RuntimeContext::create(config.devices, nullptr, nullptr,
                                                        settings_of(config));
        if (!context)
            return make_error(std::move(context.error()));
        return Internal::ContextAccess::handle(std::move(*context));
    }

    const Runtime& Runtime::set_rebalancer_enabled(bool enabled) const
    {
        Internal::ContextAccess::context(context_).execution_engine().set_rebalancer_enabled(
            enabled);
        return *this;
    }

    Result<void> Runtime::wait() const
    {
        return Internal::ContextAccess::context(context_).devices().wait_all();
    }
} // namespace SushiRuntime
