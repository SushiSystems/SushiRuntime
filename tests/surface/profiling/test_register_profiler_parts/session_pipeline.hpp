/**************************************************************************/
/* session_pipeline.hpp                                                   */
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
 * @file session_pipeline.hpp
 * @brief Declares the graphs and the session helper the profiling tests record runs with.
 * @see tests/surface/profiling/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiProfiler/config/profiler_config.hpp>
#include <SushiProfiler/json/json_reader.hpp>
#include <SushiProfiler/periods/period_timeline.hpp>
#include <SushiProfiler/record/run_record.hpp>
#include <SushiProfiler/registry/registry.hpp>
#include <SushiProfiler/session/session.hpp>

#include <SushiRuntime/profiling/register_profiler_parts.hpp>
#include <SushiRuntime/sushiruntime.hpp>

namespace SessionTests
{
    /** @brief Names the registration index of each node of the three-node pipeline, in recording order. */
    enum PipelineNode : std::uint32_t
    {
        K_LOAD = 0,  /**< Writes the first buffer; states a cost with floating-point work. */
        K_SCALE = 1, /**< Doubles it into the second; states a cost with no floating-point work. */
        K_STORE = 2  /**< Adds one into the third; states no cost. */
    };

    /**
     * @brief Owns a runtime and a graph of three kernel nodes named load, scale and store.
     *
     * Neither copied nor moved: the kernels hold the buffers' addresses.
     */
    class SessionPipeline
    {
        public:
            /** @brief The default element count every node launches over. */
            static constexpr std::size_t K_ELEMENTS = 256;

            /**
             * @brief Builds the runtime from @p config and records the three nodes over @p elements elements.
             * @param device The device whose memory the buffers live in, and so the one the nodes run on.
             */
            explicit SessionPipeline(const SushiRuntime::RuntimeConfig& config = scheduler_config(),
                                     SushiRuntime::DeviceIndex device = SushiRuntime::DeviceIndex{0},
                                     std::size_t elements = K_ELEMENTS)
                : runtime_(SushiRuntime::Runtime::create(config).value()),
                  first_(runtime_.buffer<float>(elements, device).value()),
                  second_(runtime_.buffer<float>(elements, device).value()),
                  third_(runtime_.buffer<float>(elements, device).value()),
                  graph_(runtime_.graph())
            {
                using namespace SushiRuntime;
                float* a = first_.data();
                float* b = second_.data();
                float* c = third_.data();

                NodeOptions load;
                load.name = "load";
                load.operation = "pipeline.load"_op;
                load.cost = NodeCost{elements, elements * sizeof(float), NodeDType::F32};
                NodeOptions scale;
                scale.name = "scale";
                scale.operation = "pipeline.scale"_op;
                scale.cost = NodeCost{0, 2 * elements * sizeof(float), NodeDType::F32};
                NodeOptions store;
                store.name = "store";
                store.operation = "pipeline.store"_op;

                graph_.add(elements, Reads{}, Writes(first_),
                           [a](std::size_t i) { a[i] = static_cast<float>(i); }, load);
                graph_.add(elements, Reads(first_), Writes(second_),
                           [a, b](std::size_t i) { b[i] = a[i] * 2.0f; }, scale);
                graph_.add(elements, Reads(second_), Writes(third_),
                           [b, c](std::size_t i) { c[i] = b[i] + 1.0f; }, store);
            }

            SessionPipeline(const SessionPipeline&) = delete;
            SessionPipeline& operator=(const SessionPipeline&) = delete;
            SessionPipeline(SessionPipeline&&) = delete;
            SessionPipeline& operator=(SessionPipeline&&) = delete;

            /** @brief Returns the runtime. */
            SushiRuntime::Runtime& runtime() noexcept { return runtime_; }

            /** @brief Returns the graph. */
            SushiRuntime::Graph& graph() noexcept { return graph_; }

            /** @brief Returns a config that keeps every run on the worker pool. */
            static SushiRuntime::RuntimeConfig scheduler_config()
            {
                SushiRuntime::RuntimeConfig config;
                config.force_scheduler = true;
                return config;
            }

            /** @brief Returns a config that lets a chainable plan run as an in-order chain. */
            static SushiRuntime::RuntimeConfig chain_config()
            {
                return SushiRuntime::RuntimeConfig{};
            }

        private:
            SushiRuntime::Runtime runtime_;      /**< The runtime. */
            SushiRuntime::Buffer<float> first_;  /**< Written by load. */
            SushiRuntime::Buffer<float> second_; /**< Written by scale. */
            SushiRuntime::Buffer<float> third_;  /**< Written by store. */
            SushiRuntime::Graph graph_;          /**< The recorded nodes. */
    };

    /**
     * @brief Owns a runtime and an inference-shaped graph: a stack of layers, each a matmul-like node and a relu node.
     *
     * Every layer reads the activations of the one before and its own weights, which no node writes, as an
     * inference pass reads its parameters; its first node writes a product its second node reads. The nodes carry names and costs, so a profile of a pass can be joined to them.
     */
    class InferencePipeline
    {
        public:
            /** @brief Layers in the stack. */
            static constexpr std::size_t K_LAYERS = 4;

            /** @brief Width of every activation and weight vector. */
            static constexpr std::size_t K_WIDTH = 512;

            /** @brief Builds the runtime from @p config and records two nodes per layer. */
            explicit InferencePipeline(const SushiRuntime::RuntimeConfig& config = SessionPipeline::scheduler_config())
                : runtime_(SushiRuntime::Runtime::create(config).value()), graph_(runtime_.graph())
            {
                using namespace SushiRuntime;
                activations_.reserve(K_LAYERS + 1);
                weights_.reserve(K_LAYERS);
                products_.reserve(K_LAYERS);
                for (std::size_t layer = 0; layer <= K_LAYERS; ++layer)
                    activations_.push_back(runtime_.buffer<float>(K_WIDTH).value());
                for (std::size_t layer = 0; layer < K_LAYERS; ++layer)
                {
                    weights_.push_back(runtime_.buffer<float>(K_WIDTH).value());
                    products_.push_back(runtime_.buffer<float>(K_WIDTH).value());
                }
                for (std::size_t i = 0; i < K_WIDTH; ++i)
                {
                    activations_[0][i] = 1.0f;
                    for (std::size_t layer = 0; layer < K_LAYERS; ++layer)
                        weights_[layer][i] = 0.5f;
                }

                for (std::size_t layer = 0; layer < K_LAYERS; ++layer)
                {
                    const float* in = activations_[layer].data();
                    const float* w = weights_[layer].data();
                    float* product = products_[layer].data();
                    float* out = activations_[layer + 1].data();
                    names_.push_back("layer" + std::to_string(layer) + ".matmul");
                    names_.push_back("layer" + std::to_string(layer) + ".relu");

                    NodeOptions matmul;
                    matmul.name = names_[2 * layer];
                    matmul.operation = "inference.matmul"_op;
                    matmul.cost = NodeCost{2 * K_WIDTH, 3 * K_WIDTH * sizeof(float), NodeDType::F32};
                    NodeOptions relu;
                    relu.name = names_[2 * layer + 1];
                    relu.operation = "inference.relu"_op;
                    relu.cost = NodeCost{K_WIDTH, 2 * K_WIDTH * sizeof(float), NodeDType::F32};

                    graph_.add(K_WIDTH, Reads(activations_[layer], weights_[layer]), Writes(products_[layer]),
                               [in, w, product](std::size_t i) { product[i] = in[i] * w[i] * 2.0f; }, matmul);
                    graph_.add(K_WIDTH, Reads(products_[layer]), Writes(activations_[layer + 1]),
                               [product, out](std::size_t i) { out[i] = product[i] > 0.0f ? product[i] : 0.0f; }, relu);
                }
            }

            InferencePipeline(const InferencePipeline&) = delete;
            InferencePipeline& operator=(const InferencePipeline&) = delete;
            InferencePipeline(InferencePipeline&&) = delete;
            InferencePipeline& operator=(InferencePipeline&&) = delete;

            /** @brief Returns the runtime. */
            SushiRuntime::Runtime& runtime() noexcept { return runtime_; }

            /** @brief Returns the graph. */
            SushiRuntime::Graph& graph() noexcept { return graph_; }

            /** @brief Returns the names of the nodes, in recording order. */
            const std::vector<std::string>& names() const noexcept { return names_; }

        private:
            SushiRuntime::Runtime runtime_;                          /**< The runtime. */
            std::vector<SushiRuntime::Buffer<float>> activations_;   /**< The input and the output of every layer. */
            std::vector<SushiRuntime::Buffer<float>> weights_;       /**< One vector per layer; read only. */
            std::vector<SushiRuntime::Buffer<float>> products_;      /**< What each layer's first node writes. */
            std::vector<std::string> names_;                         /**< Node labels; they outlive the recorded nodes. */
            SushiRuntime::Graph graph_;                              /**< The recorded nodes. */
    };

    /** @brief Returns the configuration of a session of @p warmup warm-up and @p active active periods of "run.step". */
    inline SushiProfiler::ProfilerConfig session_config(std::uint32_t warmup, std::uint32_t active)
    {
        SushiProfiler::ProfilerConfig config;
        config.timeline = "run.step";
        config.window.warmup = warmup;
        config.window.active = active;
        config.window.repeat = 1;
        config.sources = {SushiProfiler::NamedSettings{"sushiruntime.device", *SushiProfiler::read_json("{}").value}};
        config.buffers.linger_ms = 0;
        return config;
    }

    /**
     * @brief Registers @p runtime, starts a session and records one period per call of @p run_period.
     *
     * Marks the timeline, calls run_period(period, timeline), and repeats for @p warmup + @p active
     * periods; a last mark closes the final one. Fails the test when the session cannot start.
     * @return The record the session returns.
     */
    template <class RunPeriod>
    SushiProfiler::RunRecord record_periods(SushiRuntime::Runtime& runtime, std::uint32_t warmup,
                                            std::uint32_t active, RunPeriod&& run_period)
    {
        SushiProfiler::Registry registry;
        SushiRuntime::RuntimeProfilerParts parts = SushiRuntime::register_profiler_parts(registry, runtime);
        EXPECT_TRUE(parts.is_registered());

        SushiProfiler::SessionStart start =
            SushiProfiler::Session::start(session_config(warmup, active), registry);
        EXPECT_TRUE(start.ok()) << start.error;
        if (!start.ok())
            return {};

        SushiProfiler::PeriodTimeline& timeline = start.session->timeline("run.step");
        for (std::uint32_t period = 0; period < warmup + active; ++period)
        {
            timeline.mark();
            run_period(period, timeline);
        }
        timeline.mark();
        return std::move(start.session->stop().record);
    }

    /** @brief Returns the names of the sites whose zones @p period holds. */
    inline std::set<std::string> zone_sites(const SushiProfiler::RunRecord& record,
                                            const SushiProfiler::PeriodRecord& period)
    {
        std::set<std::string> names;
        for (const SushiProfiler::ZoneTree& tree : period.zones)
            for (const SushiProfiler::ZoneNode& node : tree.nodes)
                names.insert(record.sites[node.site].name);
        return names;
    }

    /** @brief Returns the value of integer attribute @p key of span @p row of @p period, or -1 when the row lacks it. */
    inline std::int64_t integer_attribute(const SushiProfiler::RunRecord& record,
                                          const SushiProfiler::PeriodRecord& period, const char* key,
                                          std::size_t row)
    {
        const SushiProfiler::AttributeColumn* column = period.spans.attribute(record.find_name(key));
        if (column == nullptr || row >= column->present.size() || column->present[row] == 0)
            return -1;
        return column->integers[row];
    }

    /** @brief Returns whether span @p row of @p period carries attribute @p key. */
    inline bool has_attribute(const SushiProfiler::RunRecord& record, const SushiProfiler::PeriodRecord& period,
                              const char* key, std::size_t row)
    {
        const SushiProfiler::AttributeColumn* column = period.spans.attribute(record.find_name(key));
        return column != nullptr && row < column->present.size() && column->present[row] != 0;
    }
    /** @brief Returns the sum of every sample the counter or value site @p name recorded in @p record, and how many. */
    inline std::pair<double, std::size_t> sample_total(const SushiProfiler::RunRecord& record, const char* name)
    {
        double total = 0.0;
        std::size_t samples = 0;
        for (const SushiProfiler::PeriodRecord& period : record.periods)
            for (const SushiProfiler::SampleSeries& series : period.samples)
            {
                if (record.sites[series.site].name != name)
                    continue;
                for (const double value : series.values)
                    total += value;
                samples += series.values.size();
            }
        return {total, samples};
    }

    /** @brief Returns the index of the first node of @p tree whose site is called @p name, or -1 when there is none. */
    inline int find_zone(const SushiProfiler::RunRecord& record, const SushiProfiler::ZoneTree& tree, const char* name)
    {
        for (std::size_t index = 0; index < tree.nodes.size(); ++index)
            if (record.sites[tree.nodes[index].site].name == name)
                return static_cast<int>(index);
        return -1;
    }

    /** @brief Returns the name of the site of the parent of node @p index of @p tree, or an empty string at the root. */
    inline std::string parent_zone(const SushiProfiler::RunRecord& record, const SushiProfiler::ZoneTree& tree,
                                   int index)
    {
        const std::int32_t parent = tree.nodes[static_cast<std::size_t>(index)].parent;
        return parent < 0 ? std::string() : record.sites[tree.nodes[static_cast<std::size_t>(parent)].site].name;
    }
    /**
     * @brief Records periods as record_periods() does, with no source registered, so no observer is added to the runtime.
     *
     * The runtime still writes its zones, values and counters, which need no observer.
     */
    template <class RunPeriod>
    SushiProfiler::RunRecord record_periods_without_source(std::uint32_t warmup, std::uint32_t active,
                                                           RunPeriod&& run_period)
    {
        const SushiProfiler::Registry registry;
        SushiProfiler::ProfilerConfig config = session_config(warmup, active);
        config.sources.clear();
        SushiProfiler::SessionStart start = SushiProfiler::Session::start(config, registry);
        EXPECT_TRUE(start.ok()) << start.error;
        if (!start.ok())
            return {};

        SushiProfiler::PeriodTimeline& timeline = start.session->timeline("run.step");
        for (std::uint32_t period = 0; period < warmup + active; ++period)
        {
            timeline.mark();
            run_period(period, timeline);
        }
        timeline.mark();
        return std::move(start.session->stop().record);
    }
} // namespace SessionTests
