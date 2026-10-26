/**************************************************************************/
/* register_profiler_parts.cpp                                            */
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
 * @file register_profiler_parts.cpp
 * @brief Implements RuntimeProfilerParts, which registers the runtime's source and contributor.
 * @author Mustafa Garip
 * @see modules/surface/profiling/README.md
 */

#include <SushiRuntime/profiling/register_profiler_parts.hpp>

#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <SushiRuntime/context_access/context_access.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/machine/device_description.hpp>
#include <SushiRuntime/machine/node_facts.hpp>
#include <SushiRuntime/profiling/device_source.hpp>
#include <SushiRuntime/profiling/run_feed.hpp>
#include <SushiRuntime/profiling/run_queue.hpp>
#include <SushiRuntime/profiling/source_names.hpp>
#include <SushiRuntime/profiling/runtime_environment.hpp>
#include <SushiRuntime/report/run_observer.hpp>

namespace SushiRuntime
{
    namespace
    {
        /** @brief The observations the source holds before the oldest is dropped. */
        constexpr std::size_t K_RUN_QUEUE_CAPACITY = 64;
    } // namespace

    /**
     * @brief Holds the parts the registry refers to, and adds and removes the observers around a session.
     */
    struct RuntimeProfilerParts::Impl final : public Internal::Profiling::FeedAttachment
    {
        /** @brief Names one runtime the source is fed from. */
        struct Attached
        {
            std::shared_ptr<Internal::Execution::RuntimeContext> context; /**< The runtime's context. */
            std::shared_ptr<Internal::Profiling::RunFeed> feed;           /**< Queues the runtime's runs. */
            RunObserverHandle handle;                                    /**< Removes the feed while a session runs. */
        };

        Impl() : environment(contexts) {}

        /** @brief Adds @p runtime as the next source index. */
        void add(const Runtime& runtime)
        {
            Attached attached;
            attached.context = Internal::ContextAccess::shared(runtime);
            attached.feed = std::make_shared<Internal::Profiling::RunFeed>(
                queue, static_cast<std::uint32_t>(runtimes.size()));
            contexts.push_back(attached.context);
            runtimes.push_back(std::move(attached));
        }

        /** @brief Returns the track naming data of every runtime. */
        std::vector<Internal::Profiling::RuntimeTracks> tracks() const
        {
            std::vector<Internal::Profiling::RuntimeTracks> named;
            named.reserve(runtimes.size());
            for (const Attached& attached : runtimes)
            {
                Internal::Profiling::RuntimeTracks one;
                const NodeFacts* node = attached.context->machine_facts();
                one.node = node != nullptr ? node->name : std::string("node");
                for (const DeviceDescription& device : attached.context->devices().devices())
                    one.device_names.push_back(device.name);
                named.push_back(std::move(one));
            }
            return named;
        }

        std::string attach() override
        {
            const std::lock_guard<std::mutex> lock(mutex);
            for (Attached& attached : runtimes)
                if (!attached.handle)
                    attached.handle = Internal::Execution::observe_runs(attached.context, attached.feed);
            session_running.store(true, std::memory_order_release);
            return {};
        }

        void detach() noexcept override
        {
            const std::lock_guard<std::mutex> lock(mutex);
            for (Attached& attached : runtimes)
                attached.handle.reset();
            session_running.store(false, std::memory_order_release);
        }

        /** @brief Holds the observations of every runtime until the source collects them. */
        std::shared_ptr<Internal::Profiling::RunQueue> queue =
            std::make_shared<Internal::Profiling::RunQueue>(K_RUN_QUEUE_CAPACITY);

        /** @brief The runtimes, in the order they were added; their feeds are tagged with the position. */
        std::vector<Attached> runtimes;

        /** @brief The same contexts, for the contributor. */
        std::vector<std::shared_ptr<Internal::Execution::RuntimeContext>> contexts;

        /** @brief States the facts of the runtimes. */
        Internal::Profiling::RuntimeEnvironment environment;

        /** @brief Guards the runtimes between a session's begin and end and an attach(). */
        std::mutex mutex;

        /** @brief Whether a session added the observers and has not removed them. */
        std::atomic<bool> session_running{false};

        /** @brief Whether the registry accepted the source. */
        bool registered = false;
    };

    RuntimeProfilerParts::RuntimeProfilerParts(SushiProfiler::Registry& registry, const Runtime& runtime)
        : impl_(std::make_unique<Impl>())
    {
        impl_->add(runtime);

        Impl* const impl = impl_.get();
        const auto make_source = [impl](const SushiProfiler::JsonValue& settings)
        {
            static_cast<void>(settings);
            return SushiProfiler::Made<SushiProfiler::Source>{
                std::make_unique<Internal::Profiling::DeviceSource>(*impl->queue, impl->tracks(), impl),
                std::string()};
        };
        impl_->registered =
            registry.add_source(std::string(K_DEVICE_SOURCE_NAME), make_source);
        if (impl_->registered)
            registry.add_contributor(impl_->environment);
    }

    RuntimeProfilerParts::~RuntimeProfilerParts() = default;

    bool RuntimeProfilerParts::attach(const Runtime& runtime)
    {
        const std::lock_guard<std::mutex> lock(impl_->mutex);
        if (!impl_->registered || impl_->session_running.load(std::memory_order_acquire))
            return false;
        impl_->add(runtime);
        return true;
    }

    bool RuntimeProfilerParts::is_registered() const noexcept
    {
        return impl_->registered;
    }

    RuntimeProfilerParts register_profiler_parts(SushiProfiler::Registry& registry, const Runtime& runtime)
    {
        return RuntimeProfilerParts(registry, runtime);
    }
} // namespace SushiRuntime
