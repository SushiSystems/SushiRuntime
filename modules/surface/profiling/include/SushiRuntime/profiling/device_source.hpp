/**************************************************************************/
/* device_source.hpp                                                      */
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
 * @file device_source.hpp
 * @brief Declares DeviceSource, the SushiProfiler source that reports the runtime's device work as spans.
 * @author Mustafa Garip
 * @see modules/surface/profiling/README.md
 */

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <string_view>
#include <utility>
#include <vector>

#include <SushiProfiler/sources/source.hpp>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/node_cost.hpp>
#include <SushiRuntime/profiling/run_queue.hpp>
#include <SushiRuntime/profiling/span_loss_tally.hpp>
#include <SushiRuntime/report/node_table.hpp>

namespace SushiRuntime
{
    namespace Internal::Profiling
    {
        /** @brief Names the tracks of one runtime: its node and its devices. */
        struct RuntimeTracks
        {
            std::string node;                     /**< Name of the machine the runtime runs on. */
            std::vector<std::string> device_names; /**< Name of each device, by device index. */
        };

        /**
         * @brief Starts and stops whatever delivers observations to the source, around a session.
         *
         * The parts object implements it by adding and removing the observers of its runtimes.
         */
        class FeedAttachment
        {
            public:
                virtual ~FeedAttachment() = default;

                /** @brief Starts delivering; returns why it could not, or an empty string. */
                [[nodiscard]] virtual std::string attach() = 0;

                /** @brief Stops delivering. */
                virtual void detach() noexcept = 0;
        };

        /**
         * @brief Turns the observations of the runtime's runs into spans of the period that holds their start.
         *
         * A span is placed by its converted start against the period the session hands collect(): an
         * older one is dropped late, a later one waits for its period. A device record is converted
         * through its device's pairing; a record with none is dropped. The runtime reads no period.
         * @see modules/surface/profiling/README.md
         */
        class SUSHIRUNTIME_API DeviceSource final : public SushiProfiler::Source
        {
            public:
                /**
                 * @brief Builds a source that drains @p queue and names tracks from @p runtimes.
                 * @param queue Outlives the source.
                 * @param runtimes Indexed by QueuedRun::runtime.
                 * @param attachment Started by begin() and stopped by end(); null for none. Outlives the source.
                 */
                DeviceSource(RunQueue& queue, std::vector<RuntimeTracks> runtimes,
                             FeedAttachment* attachment = nullptr,
                             std::uint64_t max_delay_ns = K_MAX_DELAY_NS);

                /** @brief The longest a run's device work may finish after its period ends: 2 000 ms. */
                static constexpr std::uint64_t K_MAX_DELAY_NS = 2'000'000'000ULL;

                [[nodiscard]] std::string_view name() const noexcept override;
                [[nodiscard]] std::uint64_t max_delay_ns() const noexcept override;
                [[nodiscard]] SushiProfiler::SourceStatus begin(SushiProfiler::SourceContext& context) override;
                void collect(const SushiProfiler::PeriodSpan& period, SushiProfiler::SpanSink& sink) override;
                void end() override;

            private:
                /** @brief One span converted to the steady clock and waiting for its period. */
                struct PendingSpan
                {
                    std::uint64_t queued_steady_ns = 0;  /**< Submit time on the steady clock. */
                    std::uint64_t start_steady_ns = 0;   /**< Start time on the steady clock; places the span. */
                    std::uint64_t end_steady_ns = 0;     /**< End time on the steady clock. */
                    std::uint32_t track = 0;             /**< Track the span is on. */
                    std::uint32_t name = 0;              /**< Interned span name. */
                    std::uint32_t submit_thread = 0;     /**< Profiler thread index of the submitter. */
                    std::uint32_t uncertainty_ns = 0;    /**< Saturated pairing uncertainty; 0 for a host span. */
                    std::uint32_t node = 0;              /**< Registration index. */
                    std::uint32_t invocation = 0;        /**< Step index within the run. */
                    std::uint32_t operation = 0;         /**< Operation hash; 0 for none. */
                    std::uint32_t launched_width = 0;    /**< Launch width, or K_WIDTH_UNKNOWN. */
                    std::uint32_t live_width = 0;        /**< Live width, or K_WIDTH_UNKNOWN. */
                    std::uint32_t layer = 0;             /**< Interned layer name. */
                    std::uint32_t dtype = 0;             /**< Interned dtype name; 0 for none. */
                    std::uint32_t graph = 0;             /**< Interned graph label; 0 for none. */
                    std::uint32_t origin = 0;            /**< Interned origin of the node; 0 for none. */
                    bool has_cost = false;               /**< Whether the node stated a cost. */
                    std::uint64_t flops = 0;             /**< Stated floating-point operations. */
                    std::uint64_t bytes = 0;             /**< Stated bytes moved. */
                };

                /** @brief Moves the queued observations into pending_ as converted spans; counts what it drops in @p sink. */
                void ingest(SushiProfiler::SpanSink& sink);

                /** @brief The names a table's strings were interned as, so none is interned per span. */
                struct TableNames
                {
                    std::shared_ptr<const NodeTable> table;             /**< Keeps the table, and so its address, alive. */
                    std::uint32_t graph = 0;                            /**< Interned label; 0 for none. */
                    std::unordered_map<std::uint32_t, std::uint32_t> origins; /**< Interned origin by registration index; 0 for none. */
                };

                /** @brief Returns the names of @p table, interning its label on first sight. */
                TableNames& names_of(const std::shared_ptr<const NodeTable>& table);

                /** @brief Returns the interned origin of @p row of @p names, interning it on first sight. */
                std::uint32_t origin_of(TableNames& names, const NodeTableRow& row);

                /** @brief Returns the track of @p device of @p runtime, adding it on first use. */
                std::uint32_t track_of(std::uint32_t runtime, std::uint32_t device);

                /** @brief Writes @p span into @p sink with its attributes. */
                void write(const PendingSpan& span, SushiProfiler::SpanSink& sink);

                /** @brief The queue the run threads fill. */
                RunQueue& queue_;

                /** @brief Track naming data by runtime. */
                std::vector<RuntimeTracks> runtimes_;

                /** @brief Starts and stops the delivery around a session; may be null. */
                FeedAttachment* attachment_;

                /** @brief What max_delay_ns() reports. */
                std::uint64_t max_delay_ns_;

                /** @brief The context of the running session; null outside one. */
                SushiProfiler::SourceContext* context_ = nullptr;

                /** @brief Tracks added so far, by runtime and device. */
                std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> tracks_;

                /** @brief The interned strings of every table seen, by table address. */
                std::unordered_map<const NodeTable*, TableNames> table_names_;

                /** @brief Spans converted and not yet placed, in arrival order. */
                std::vector<PendingSpan> pending_;

                /** @brief The spans lost since the last log line, by cause. */
                SpanLossTally losses_;

                /** @brief The interned attribute keys, valid after begin(). */
                struct Keys
                {
                    std::uint32_t node = 0;
                    std::uint32_t invocation = 0;
                    std::uint32_t operation = 0;
                    std::uint32_t launched_width = 0;
                    std::uint32_t live_width = 0;
                    std::uint32_t flops = 0;
                    std::uint32_t bytes = 0;
                    std::uint32_t dtype = 0;
                    std::uint32_t graph = 0;
                    std::uint32_t origin = 0;
                    std::uint32_t layer = 0;
                    std::uint32_t layer_scheduler = 0;
                    std::uint32_t layer_chain = 0;
                } keys_;
        };
    } // namespace Profiling
} // namespace SushiRuntime
