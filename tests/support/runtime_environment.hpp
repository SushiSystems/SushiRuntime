/**************************************************************************/
/* runtime_environment.hpp                                                */
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
 * @file runtime_environment.hpp
 * @brief Holds the runtimes and the one device queue of a test program, and the fixture base that hands them out.
 * @see tests/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <tuple>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>

/**
 * @brief Test helpers shared by the modules of this repository.
 */
namespace TestSupport
{
/**
 * @brief Names the configuration of a shared runtime: the fields of RuntimeConfig the tests vary.
 *
 * The default shape is the default RuntimeConfig, the runtime every medium test shares.
 */
struct RuntimeShape
{
    bool force_scheduler = false; /**< Whether every run is kept on the scheduler. */
    std::size_t worker_count = 0; /**< The pool's worker count; 0 is the whole machine. */
    bool rebalancer = false;      /**< Whether the background rebalancer runs. */

    /** @brief Orders shapes field by field, so a shape can key the environment's runtimes. */
    [[nodiscard]] friend bool operator<(const RuntimeShape& left, const RuntimeShape& right) noexcept
    {
        return std::tie(left.force_scheduler, left.worker_count, left.rebalancer) <
               std::tie(right.force_scheduler, right.worker_count, right.rebalancer);
    }

    /** @brief Returns the RuntimeConfig this shape names. */
    [[nodiscard]] SushiRuntime::RuntimeConfig config() const
    {
        SushiRuntime::RuntimeConfig made;
        made.force_scheduler = force_scheduler;
        made.worker_count = worker_count;
        made.rebalancer = rebalancer;
        return made;
    }
};

/**
 * @brief Owns the runtimes and the device queue of the program, each made on first use and released after the
 *        last test.
 */
class RuntimeEnvironment final : public ::testing::Environment
{
    public:
        /**
         * @brief Returns the runtime of the program, creating it on the first call.
         * @return The runtime every medium test of the program shares.
         */
        [[nodiscard]] static SushiRuntime::Runtime& runtime()
        {
            return runtime(RuntimeShape{});
        }

        /**
         * @brief Returns the program's runtime of one shape, creating it on the first call for that shape.
         * @param shape The configuration the runtime is built with.
         * @return The runtime every medium test that asks for this shape shares.
         */
        [[nodiscard]] static SushiRuntime::Runtime& runtime(const RuntimeShape& shape)
        {
            std::map<RuntimeShape, SushiRuntime::Runtime>& held = held_runtimes();
            auto found = held.find(shape);
            if (found == held.end())
                found = held.emplace(shape, SushiRuntime::Runtime::create(shape.config()).value()).first;
            return found->second;
        }

        /**
         * @brief Returns the program's runtime that keeps every run on the scheduler, or the default one.
         * @param force_scheduler True for the runtime forced onto the scheduler, false for the default one.
         * @return The runtime of a row of the tests that run once on each layer.
         */
        [[nodiscard]] static SushiRuntime::Runtime& layer_runtime(bool force_scheduler)
        {
            RuntimeShape shape;
            shape.force_scheduler = force_scheduler;
            return runtime(shape);
        }

        /**
         * @brief Returns the device queue of the program, creating it on the first call.
         * @return The queue every medium test that submits its own work shares.
         */
        [[nodiscard]] static sycl::queue& queue()
        {
            std::optional<sycl::queue>& held = held_queue();
            if (!held)
                held.emplace();
            return *held;
        }

        /**
         * @brief Releases the queue and the runtimes after the last test of the program.
         */
        void TearDown() override
        {
            held_queue().reset();
            held_runtimes().clear();
        }

    private:
        /** @brief Returns the program's runtimes, keyed by their shape. */
        static std::map<RuntimeShape, SushiRuntime::Runtime>& held_runtimes()
        {
            static std::map<RuntimeShape, SushiRuntime::Runtime> slots;
            return slots;
        }

        /** @brief Returns the slot of the program's queue. */
        static std::optional<sycl::queue>& held_queue()
        {
            static std::optional<sycl::queue> slot;
            return slot;
        }
};

/**
 * @brief Registers the environment once per program, whatever number of units include this header.
 */
inline ::testing::Environment* const K_RUNTIME_ENVIRONMENT =
    ::testing::AddGlobalTestEnvironment(new RuntimeEnvironment());

/**
 * @brief Fixture base of every medium test that needs the runtime or the device.
 */
class RuntimeTest : public ::testing::Test
{
    protected:
        /** @brief Returns the runtime of the program. */
        [[nodiscard]] static SushiRuntime::Runtime& runtime() { return RuntimeEnvironment::runtime(); }

        /** @brief Returns the device queue of the program. */
        [[nodiscard]] static sycl::queue& queue() { return RuntimeEnvironment::queue(); }
};

/**
 * @brief Hands a fixture the program's runtime of its row: the default one for the in-order chain, the one forced
 *        onto the scheduler otherwise.
 */
class LayerRuntime
{
    protected:
        /** @brief Chooses the runtime of the row; true is the one forced onto the scheduler. */
        void use_layer(bool force_scheduler) noexcept { force_scheduler_ = force_scheduler; }

        /** @brief Returns the runtime of the row. */
        [[nodiscard]] SushiRuntime::Runtime& runtime() const
        {
            return RuntimeEnvironment::layer_runtime(force_scheduler_);
        }

    private:
        bool force_scheduler_ = false; /**< Whether the row runs on the runtime forced onto the scheduler. */
};

/**
 * @brief Fixture base of the rows that run once on the in-order chain (false) and once on the scheduler (true).
 */
class LayerRuntimeTest : public ::testing::TestWithParam<bool>, protected LayerRuntime
{
    protected:
        /** @brief Chooses the runtime of the row from its parameter. */
        void SetUp() override { use_layer(GetParam()); }
};
} // namespace TestSupport
