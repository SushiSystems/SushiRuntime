/**************************************************************************/
/* device_source.cpp                                                      */
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
 * @file device_source.cpp
 * @brief Implements DeviceSource, which turns the runtime's observations into spans of the profiler's periods.
 * @author Mustafa Garip
 * @see modules/surface/profiling/README.md
 */

#include <SushiRuntime/profiling/device_source.hpp>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <utility>

#include <SushiProfiler/record/device_span_keys.hpp>
#include <SushiProfiler/record/span_table.hpp>
#include <SushiProfiler/sources/source_context.hpp>
#include <SushiProfiler/sources/span_sink.hpp>

#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/profile/profile_keys.hpp>
#include <SushiRuntime/profiling/clock_conversion.hpp>
#include <SushiRuntime/profiling/device_track_name.hpp>
#include <SushiRuntime/profiling/profiling_log_category.hpp>
#include <SushiRuntime/profiling/source_names.hpp>
#include <SushiRuntime/report/clock_pairing.hpp>
#include <SushiRuntime/report/dispatch_width.hpp>
#include <SushiRuntime/report/node_event_record.hpp>

namespace SushiRuntime
{
    namespace Internal::Profiling
    {
        namespace
        {
            /** @brief Returns the pairing of @p device in @p observation, or null when the run paired none. */
            const ClockPairing* pairing_of(const RunObservation& observation, std::uint32_t device) noexcept
            {
                for (const DeviceClock& clock : observation.device_clocks)
                    if (clock.device_index == device)
                        return &clock.pairing;
                return nullptr;
            }
        } // namespace

        DeviceSource::DeviceSource(RunQueue& queue, std::vector<RuntimeTracks> runtimes,
                                   FeedAttachment* attachment)
            : queue_(queue), runtimes_(std::move(runtimes)), attachment_(attachment)
        {
        }

        std::string_view DeviceSource::name() const noexcept
        {
            return K_DEVICE_SOURCE_NAME;
        }

        SushiProfiler::SourceStatus DeviceSource::begin(SushiProfiler::SourceContext& context)
        {
            namespace Keys = SushiProfiler::DeviceSpanKeys;

            context_ = &context;
            keys_.node = context.intern(Keys::K_NODE);
            keys_.invocation = context.intern(Keys::K_INVOCATION);
            keys_.operation = context.intern(Keys::K_OPERATION);
            keys_.launched_width = context.intern(Keys::K_LAUNCHED_WIDTH);
            keys_.live_width = context.intern(Keys::K_LIVE_WIDTH);
            keys_.flops = context.intern(Keys::K_FLOPS);
            keys_.bytes = context.intern(Keys::K_BYTES);
            keys_.dtype = context.intern(Keys::K_DTYPE);
            keys_.graph = context.intern(Keys::K_GRAPH);
            keys_.origin = context.intern(Keys::K_ORIGIN);
            keys_.layer = context.intern(Profile::K_KEY_LAYER);
            keys_.layer_scheduler = context.intern(Profile::K_LAYER_SCHEDULER);
            keys_.layer_chain = context.intern(Profile::K_LAYER_CHAIN);

            if (attachment_ != nullptr)
            {
                std::string failure = attachment_->attach();
                if (!failure.empty())
                {
                    context_ = nullptr;
                    return SushiProfiler::SourceStatus{std::move(failure)};
                }
            }
            return {};
        }

        std::uint32_t DeviceSource::track_of(std::uint32_t runtime, std::uint32_t device)
        {
            const auto key = std::make_pair(runtime, device);
            const auto found = tracks_.find(key);
            if (found != tracks_.end())
                return found->second;

            const RuntimeTracks& tracks = runtimes_[runtime];
            const std::string name = device == K_HOST_DEVICE_INDEX
                ? host_track_name(tracks.node)
                : device_track_name(tracks.node, device,
                                    device < tracks.device_names.size()
                                        ? std::string_view(tracks.device_names[device])
                                        : std::string_view("device"));
            const std::uint32_t track = context_->add_track(name);
            tracks_.emplace(key, track);
            return track;
        }

        DeviceSource::TableNames& DeviceSource::names_of(const std::shared_ptr<const NodeTable>& table)
        {
            const auto found = table_names_.find(table.get());
            if (found != table_names_.end())
                return found->second;
            TableNames names;
            names.table = table;
            if (!table->label().empty())
                names.graph = context_->intern(table->label());
            return table_names_.emplace(table.get(), std::move(names)).first->second;
        }

        std::uint32_t DeviceSource::origin_of(TableNames& names, const NodeTableRow& row)
        {
            const auto found = names.origins.find(row.index);
            if (found != names.origins.end())
                return found->second;
            const std::uint32_t id = row.origin.empty() ? 0U : context_->intern(row.origin);
            names.origins.emplace(row.index, id);
            return id;
        }

        void DeviceSource::ingest(SushiProfiler::SpanSink& sink)
        {
            for (QueuedRun& run : queue_.take())
            {
                if (run.runtime >= runtimes_.size())
                    continue;

                const RunObservation& observation = run.observation;
                sink.drop_late(observation.drops.total());

                const std::uint32_t layer = observation.layer == ExecutionLayer::Chain
                    ? keys_.layer_chain : keys_.layer_scheduler;
                for (const NodeEventRecord& record : observation.events)
                {
                    PendingSpan span;
                    if (record.device_index == K_HOST_DEVICE_INDEX)
                    {
                        span.queued_steady_ns = record.submit_ns;
                        span.start_steady_ns = record.start_ns;
                        span.end_steady_ns = record.end_ns;
                    }
                    else
                    {
                        const ClockPairing* pairing = pairing_of(observation, record.device_index);
                        if (pairing == nullptr)
                        {
                            // Without its device's pairing a span has no start on the steady clock, so no period.
                            sink.drop_late(1U);
                            continue;
                        }
                        span.queued_steady_ns = to_steady_ns(*pairing, record.submit_ns);
                        span.start_steady_ns = to_steady_ns(*pairing, record.start_ns);
                        span.end_steady_ns = to_steady_ns(*pairing, record.end_ns);
                        span.uncertainty_ns = saturate_uncertainty(pairing->uncertainty_ns);
                    }
                    span.queued_steady_ns = std::min(span.queued_steady_ns, span.start_steady_ns);
                    span.end_steady_ns = std::max(span.end_steady_ns, span.start_steady_ns);

                    span.track = track_of(run.runtime, record.device_index);
                    span.submit_thread = record.submit_thread;
                    span.node = record.node_index;
                    span.invocation = record.invocation;
                    span.launched_width = record.launched_width;
                    span.live_width = record.live_width;
                    span.layer = layer;

                    const NodeTableRow* row = observation.table ? observation.table->find(record.node_index) : nullptr;
                    TableNames* names = observation.table ? &names_of(observation.table) : nullptr;
                    if (names != nullptr)
                        span.graph = names->graph;
                    if (row != nullptr)
                    {
                        span.origin = origin_of(*names, *row);
                        span.name = context_->intern(row->name);
                        span.operation = row->operation.value;
                        if (row->cost.has_value())
                        {
                            span.has_cost = true;
                            span.flops = row->cost->flops;
                            span.bytes = row->cost->bytes;
                            if (const char* dtype = dtype_name(row->cost->dtype))
                                span.dtype = context_->intern(dtype);
                        }
                    }
                    else
                    {
                        span.name = context_->intern("node " + std::to_string(record.node_index));
                    }
                    pending_.push_back(span);
                }
            }
        }

        void DeviceSource::write(const PendingSpan& pending, SushiProfiler::SpanSink& sink)
        {
            SushiProfiler::Span span;
            span.queued_ns = context_->to_record_ns(pending.queued_steady_ns);
            span.start_ns = context_->to_record_ns(pending.start_steady_ns);
            span.end_ns = context_->to_record_ns(pending.end_steady_ns);
            span.track = pending.track;
            span.name = pending.name;
            span.submit_thread = pending.submit_thread;
            span.uncertainty_ns = pending.uncertainty_ns;

            const std::optional<std::size_t> row = sink.append(span);
            if (!row)
                return;

            static_cast<void>(sink.set_integer(*row, keys_.node, static_cast<std::int64_t>(pending.node)));
            static_cast<void>(sink.set_integer(*row, keys_.invocation, static_cast<std::int64_t>(pending.invocation)));
            if (pending.operation != 0U)
                static_cast<void>(sink.set_integer(*row, keys_.operation, static_cast<std::int64_t>(pending.operation)));
            if (pending.launched_width != K_WIDTH_UNKNOWN)
                static_cast<void>(sink.set_integer(*row, keys_.launched_width, static_cast<std::int64_t>(pending.launched_width)));
            if (pending.live_width != K_WIDTH_UNKNOWN)
                static_cast<void>(sink.set_integer(*row, keys_.live_width, static_cast<std::int64_t>(pending.live_width)));
            static_cast<void>(sink.set_name(*row, keys_.layer, pending.layer));
            if (pending.graph != 0U)
                static_cast<void>(sink.set_name(*row, keys_.graph, pending.graph));
            if (pending.origin != 0U)
                static_cast<void>(sink.set_name(*row, keys_.origin, pending.origin));
            if (pending.has_cost)
            {
                static_cast<void>(sink.set_integer(*row, keys_.flops, static_cast<std::int64_t>(pending.flops)));
                static_cast<void>(sink.set_integer(*row, keys_.bytes, static_cast<std::int64_t>(pending.bytes)));
                if (pending.dtype != 0U)
                    static_cast<void>(sink.set_name(*row, keys_.dtype, pending.dtype));
            }
        }

        void DeviceSource::collect(const SushiProfiler::PeriodSpan& period, SushiProfiler::SpanSink& sink)
        {
            ingest(sink);
            sink.drop_late(queue_.take_overflow());

            // A span goes in the period whose steady span holds its start; an older one is late, a later one waits.
            std::vector<PendingSpan> waiting;
            for (const PendingSpan& span : pending_)
            {
                if (span.start_steady_ns >= period.end_steady_ns)
                    waiting.push_back(span);
                else if (span.start_steady_ns < period.start_steady_ns)
                    sink.drop_late(1U);
                else
                    write(span, sink);
            }
            pending_ = std::move(waiting);
        }

        void DeviceSource::end()
        {
            if (attachment_ != nullptr)
                attachment_->detach();
            const std::size_t unread = queue_.discard();
            if (unread != 0 || !pending_.empty())
                SR_LOG_DEBUG(K_PROFILING_LOG_CATEGORY, "The session ended with {} observations and {} spans no period took.",
                             unread, pending_.size());
            pending_.clear();
            tracks_.clear();
            table_names_.clear();
            context_ = nullptr;
        }
    } // namespace Profiling
} // namespace SushiRuntime
