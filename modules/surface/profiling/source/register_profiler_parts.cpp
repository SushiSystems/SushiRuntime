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

#include <SushiProfiler/collector/collector.hpp>

#include <SushiRuntime/machine/device_description.hpp>
#include <SushiRuntime/profiling/device_source.hpp>
#include <SushiRuntime/profiling/run_feed.hpp>
#include <SushiRuntime/profiling/run_queue.hpp>
#include <SushiRuntime/profiling/source_names.hpp>
#include <SushiRuntime/profiling/runtime_environment.hpp>
#include <SushiRuntime/report/node_event_record.hpp>
#include <SushiRuntime/report/run_observer.hpp>

namespace SushiRuntime
{
    namespace
    {
        /** @brief Answers whether the profiler's session wants its sources to measure; a paused one does not. */
        bool sources_wanted() noexcept
        {
            return SushiProfiler::Collector::sources_wanted();
        }

        /** @brief The memory the source queue may hold, in bytes: 4 MiB. */
        constexpr std::size_t K_RUN_QUEUE_BUDGET_BYTES = std::size_t{4} * 1024 * 1024;

        /**
         * @brief The events the source queue holds before the oldest runs are dropped.
         *
         * An event is the memory of a queued run, so the bound counts events: the budget of
         * K_RUN_QUEUE_BUDGET_BYTES divided by sizeof(NodeEventRecord). A record is 48 bytes, so
         * 4 194 304 / 48 = 87 381 events.
         */
        constexpr std::size_t K_RUN_QUEUE_MAX_EVENTS = K_RUN_QUEUE_BUDGET_BYTES / sizeof(NodeEventRecord);
    } // namespace

    /**
     * @brief Holds the parts the registry refers to, and adds and removes the observers around a session.
     */
    struct RuntimeProfilerParts::Impl final : public Internal::Profiling::FeedAttachment
    {
        /** @brief Names one runtime the source is fed from. */
        struct Attached
        {
            std::shared_ptr<ProfiledRuntime> runtime;           /**< What the profiler reads of the runtime. */
            std::shared_ptr<Internal::Profiling::RunFeed> feed; /**< Queues the runtime's runs. */
            RunObserverHandle handle;                           /**< Removes the feed while a session runs. */
        };

        Impl() : environment(profiled) {}

        /** @brief Adds @p runtime as the next source index. */
        void add(std::shared_ptr<ProfiledRuntime> runtime)
        {
            Attached attached;
            attached.runtime = std::move(runtime);
            attached.feed = std::make_shared<Internal::Profiling::RunFeed>(
                queue, static_cast<std::uint32_t>(runtimes.size()));
            profiled.push_back(attached.runtime);
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
                one.node = attached.runtime->node_name();
                for (const DeviceDescription& device : attached.runtime->devices())
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
                    attached.handle = attached.runtime->observe_runs(attached.feed, &sources_wanted);
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
            std::make_shared<Internal::Profiling::RunQueue>(K_RUN_QUEUE_MAX_EVENTS);

        /** @brief The runtimes, in the order they were added; their feeds are tagged with the position. */
        std::vector<Attached> runtimes;

        /** @brief The same runtimes, for the contributor. */
        std::vector<std::shared_ptr<ProfiledRuntime>> profiled;

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
        : RuntimeProfilerParts(registry, profiled_runtime_of(runtime))
    {
    }

    RuntimeProfilerParts::RuntimeProfilerParts(SushiProfiler::Registry& registry,
                                               std::shared_ptr<ProfiledRuntime> runtime)
        : impl_(std::make_unique<Impl>())
    {
        impl_->add(std::move(runtime));

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
        return attach(profiled_runtime_of(runtime));
    }

    bool RuntimeProfilerParts::attach(std::shared_ptr<ProfiledRuntime> runtime)
    {
        const std::lock_guard<std::mutex> lock(impl_->mutex);
        if (!impl_->registered || impl_->session_running.load(std::memory_order_acquire))
            return false;
        impl_->add(std::move(runtime));
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

    RuntimeProfilerParts register_profiler_parts(SushiProfiler::Registry& registry,
                                                 std::shared_ptr<ProfiledRuntime> runtime)
    {
        return RuntimeProfilerParts(registry, std::move(runtime));
    }
} // namespace SushiRuntime
