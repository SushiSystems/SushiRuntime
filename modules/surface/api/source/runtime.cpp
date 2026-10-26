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
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

#include <SushiRuntime/api/detail/assertion_logging.hpp>
#include <SushiRuntime/context_access/context_access.hpp>
#include <SushiRuntime/core/assert.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/error_translation.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/execution/runtime_settings.hpp>
#include <SushiRuntime/machine/device_description.hpp>
#include <SushiRuntime/machine/device_facts.hpp>
#include <SushiRuntime/profile/profile.hpp>
#include <SushiRuntime/report/run_observer.hpp>
#include <SushiRuntime/topology/host_identity.hpp>

namespace SushiRuntime
{
    namespace
    {
        /** @brief Returns the settings the context takes from a RuntimeConfig. */
        [[nodiscard]] Internal::Execution::RuntimeSettings
        settings_of(const RuntimeConfig& config) noexcept
        {
            Internal::Execution::RuntimeSettings settings;
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
            facts.operating_system = Internal::Topology::host_operating_system();
            facts.labels = config.labels;
            return facts;
        }

        /** @brief Returns the message of a rule change refused because a run is in flight. */
        [[nodiscard]] std::string rules_refusal(const char* call)
        {
            return std::string("Runtime::") + call +
                   ": a run of this runtime is in flight; wait for it to finish, then call again.";
        }

        /** @brief Returns whether a class name is non-empty and has no empty dotted segment. */
        [[nodiscard]] bool is_dotted_name(std::string_view name) noexcept
        {
            if (name.empty() || name.front() == '.' || name.back() == '.')
                return false;
            return name.find("..") == std::string_view::npos;
        }
    } // namespace

    Result<std::shared_ptr<NodeFacts>> Runtime::create_topology(const RuntimeConfig& config)
    {
        std::optional<Error> refusal;
        std::shared_ptr<NodeFacts> facts;
        Result<void> translated = Detail::translate_exceptions(
            [&config, &refusal, &facts]()
            {
                refusal = labels_refusal(config.labels);
                if (refusal)
                    return;
                std::string name = config.node_name.empty() ? Internal::Topology::host_name()
                                                            : config.node_name;
                if (name.empty())
                {
                    refusal = Error{ErrorCode::Unsupported,
                                    "The host name is unreadable; set RuntimeConfig::node_name."};
                    return;
                }
                facts = std::make_shared<NodeFacts>(facts_of(config, std::move(name)));
            });
        if (!translated)
            return make_error(std::move(translated.error()));
        if (refusal)
            return make_error(std::move(*refusal));
        return facts;
    }

    Result<void> Runtime::attach_facts(const ContextHandle& context,
                                       std::shared_ptr<NodeFacts> facts)
    {
        return Detail::translate_exceptions(
            [&context, &facts]()
            {
                Internal::Execution::RuntimeContext& runtime =
                    Internal::ContextAccess::context(context);
                const std::vector<DeviceDescription>& devices = runtime.devices().devices();
                facts->devices.reserve(devices.size());
                for (const DeviceDescription& device : devices)
                    facts->devices.push_back(
                        DeviceFacts{device.kind, device.name, device.memory_bytes});
                runtime.set_machine_facts(std::move(facts));
            });
    }

    Result<ContextHandle> Runtime::create_context(const RuntimeConfig& config)
    {
        Detail::install_fatal_logging();
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
        return *Internal::ContextAccess::context(context_).machine_facts();
    }

    PlacementScope Runtime::placement_scope(std::string_view name,
                                            std::optional<std::uint64_t> key) const
    {
        SR_VERIFY(is_dotted_name(name),
                  (std::string("Runtime::placement_scope: the class name \"") +
                   std::string(name) +
                   "\" is empty, starts or ends with '.' or holds two '.' in a row; pass a "
                   "dotted name such as \"sushiai.replica\".")
                      .c_str());
        return PlacementScope(PlacementClass(name.data(), name.size()), key, context_);
    }

    Result<void> Runtime::set_placement_rules(PlacementRules rules) const
    {
        Internal::Execution::RuntimeContext& context = Internal::ContextAccess::context(context_);
        // One hold against run start makes the check and the swap a single step.
        if (!context.set_placement_rules(std::make_shared<const PlacementRules>(std::move(rules))))
            return make_error(
                Error{ErrorCode::InvalidState, rules_refusal("set_placement_rules")});
        return {};
    }

    Result<void> Runtime::clear_placement_rules() const
    {
        Internal::Execution::RuntimeContext& context = Internal::ContextAccess::context(context_);
        // One hold against run start makes the check and the swap a single step.
        if (!context.set_placement_rules(nullptr))
            return make_error(
                Error{ErrorCode::InvalidState, rules_refusal("clear_placement_rules")});
        return {};
    }

    RunObserverHandle Runtime::add_run_observer(std::shared_ptr<RunObserver> observer) const
    {
        return Internal::Execution::observe_runs(Internal::ContextAccess::shared(context_),
                                                 std::move(observer));
    }

    Result<void> Runtime::wait() const
    {
        SR_ZONE("wait.runtime");
        return Internal::ContextAccess::context(context_).devices().wait_all();
    }
} // namespace SushiRuntime
