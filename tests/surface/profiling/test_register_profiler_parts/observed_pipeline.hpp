/**************************************************************************/
/* observed_pipeline.hpp                                                  */
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
 * @file observed_pipeline.hpp
 * @brief Declares ObservedPipeline, a runtime and a graph of three named nodes the observer tests run.
 * @see tests/surface/profiling/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include <SushiRuntime/sushiruntime.hpp>

namespace ObserverTests
{
    /** @brief Names the registration index of each node of the pipeline, in recording order. */
    enum PipelineNode : std::uint32_t
    {
        K_LOAD = 0,  /**< Writes the first buffer; states a cost with floating-point work. */
        K_SCALE = 1, /**< Doubles it into the second; states a cost with no floating-point work. */
        K_STORE = 2  /**< Adds one into the third; states no cost. */
    };

    /**
     * @brief Owns a runtime and a graph of three kernel nodes named load, scale and store.
     *
     * The nodes carry names, operations and, for the first two, a cost, so a test can join an
     * observed record to what the caller stated. The pipeline is neither copied nor moved: the
     * kernels hold the buffers' addresses.
     */
    class ObservedPipeline
    {
        public:
            /** @brief The element count every node launches over. */
            static constexpr std::size_t K_ELEMENTS = 256;

            /** @brief Builds the runtime from @p config and records the three nodes. */
            explicit ObservedPipeline(const SushiRuntime::RuntimeConfig& config = scheduler_config())
                : runtime_(SushiRuntime::Runtime::create(config).value()),
                  first_(runtime_.buffer<float>(K_ELEMENTS).value()),
                  second_(runtime_.buffer<float>(K_ELEMENTS).value()),
                  third_(runtime_.buffer<float>(K_ELEMENTS).value()),
                  graph_(runtime_.graph())
            {
                using namespace SushiRuntime;
                float* a = first_.data();
                float* b = second_.data();
                float* c = third_.data();

                NodeOptions load;
                load.name = "load";
                load.operation = "pipeline.load"_op;
                load.cost = NodeCost{K_ELEMENTS, K_ELEMENTS * sizeof(float), NodeDType::F32};
                NodeOptions scale;
                scale.name = "scale";
                scale.operation = "pipeline.scale"_op;
                scale.cost = NodeCost{0, 2 * K_ELEMENTS * sizeof(float), NodeDType::F32};
                NodeOptions store;
                store.name = "store";
                store.operation = "pipeline.store"_op;

                graph_.add(K_ELEMENTS, Reads{}, Writes(first_),
                           [a](std::size_t i) { a[i] = static_cast<float>(i); }, load);
                graph_.add(K_ELEMENTS, Reads(first_), Writes(second_),
                           [a, b](std::size_t i) { b[i] = a[i] * 2.0f; }, scale);
                graph_.add(K_ELEMENTS, Reads(second_), Writes(third_),
                           [b, c](std::size_t i) { c[i] = b[i] + 1.0f; }, store);
            }

            ObservedPipeline(const ObservedPipeline&) = delete;
            ObservedPipeline& operator=(const ObservedPipeline&) = delete;
            ObservedPipeline(ObservedPipeline&&) = delete;
            ObservedPipeline& operator=(ObservedPipeline&&) = delete;

            /** @brief Returns the runtime. */
            SushiRuntime::Runtime& runtime() noexcept { return runtime_; }

            /** @brief Returns the graph. */
            SushiRuntime::Graph& graph() noexcept { return graph_; }

            /** @brief Returns the last buffer, which holds 2 * i + 1 after a run. */
            SushiRuntime::Buffer<float>& result() noexcept { return third_; }

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
            SushiRuntime::Runtime runtime_;          /**< The runtime. */
            SushiRuntime::Buffer<float> first_;      /**< Written by load. */
            SushiRuntime::Buffer<float> second_;     /**< Written by scale. */
            SushiRuntime::Buffer<float> third_;      /**< Written by store. */
            SushiRuntime::Graph graph_;              /**< The recorded nodes. */
    };
} // namespace ObserverTests
