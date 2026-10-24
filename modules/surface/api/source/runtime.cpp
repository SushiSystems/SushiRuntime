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

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

#include <SushiRuntime/context_access/context_access.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/error_translation.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/execution/runtime_settings.hpp>
#include <SushiRuntime/machine/device_description.hpp>
#include <SushiRuntime/machine/device_facts.hpp>

#include "host_identity.hpp"

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

        /** @brief Returns the refusal of the first label whose key breaks a rule, if any. */
        [[nodiscard]] std::optional<Error> labels_refusal(const std::vector<NodeLabel>& labels)
        {
            std::unordered_set<std::string> seen;
            for (std::size_t position = 0; position < labels.size(); ++position)
            {
                const std::string& key = labels[position].key;
                const std::string where = "RuntimeConfig::labels, position " +
                                          std::to_string(position) + ", key \"" + key + "\": ";
                if (key.empty())
                    return Error{ErrorCode::InvalidArgument,
                                 where + "the key is empty; pass a key of at least one character."};
                if (key.find('=') != std::string::npos)
                    return Error{ErrorCode::InvalidArgument,
                                 where + "the key holds '='; pass the key without it, and put "
                                         "what follows the '=' in the value."};
                if (!seen.insert(key).second)
                    return Error{ErrorCode::InvalidArgument,
                                 where + "an earlier label has the same key; pass each key once."};
            }
            return std::nullopt;
        }

        /** @brief Returns the facts of this machine without its devices. */
        [[nodiscard]] NodeFacts facts_of(const RuntimeConfig& config, std::string name)
        {
            NodeFacts facts;
            facts.name = std::move(name);
            facts.operating_system = Detail::host_operating_system();
            facts.labels = config.labels;
            return facts;
        }
    } // namespace

    Result<NodeFacts> Runtime::create_topology(const RuntimeConfig& config)
    {
        std::optional<Error> refusal;
        NodeFacts facts;
        Result<void> translated = Detail::translate_exceptions(
            [&config, &refusal, &facts]()
            {
                refusal = labels_refusal(config.labels);
                if (refusal)
                    return;
                std::string name = config.node_name.empty() ? Detail::host_name() : config.node_name;
                if (name.empty())
                {
                    refusal = Error{ErrorCode::Unsupported,
                                    "The host name is unreadable; set RuntimeConfig::node_name."};
                    return;
                }
                facts = facts_of(config, std::move(name));
            });
        if (!translated)
            return make_error(std::move(translated.error()));
        if (refusal)
            return make_error(std::move(*refusal));
        return facts;
    }

    Result<void> Runtime::list_devices(const ContextHandle& context, NodeFacts& facts)
    {
        return Detail::translate_exceptions(
            [&context, &facts]()
            {
                const std::vector<DeviceDescription>& devices =
                    Internal::ContextAccess::context(context).devices().devices();
                facts.devices.reserve(devices.size());
                for (const DeviceDescription& device : devices)
                    facts.devices.push_back(
                        DeviceFacts{device.kind, device.name, device.memory_bytes});
            });
    }

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

    const NodeFacts& Runtime::topology() const
    {
        return topology_;
    }

    Result<void> Runtime::wait() const
    {
        return Internal::ContextAccess::context(context_).devices().wait_all();
    }
} // namespace SushiRuntime
