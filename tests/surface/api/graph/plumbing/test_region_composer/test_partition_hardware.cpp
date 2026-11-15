/**************************************************************************/
/* test_partition_hardware.cpp                                            */
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
 * @file test_partition_hardware.cpp
 * @brief Measures the three graphs of the H9 acceptance on a machine with a CUDA GPU, a HIP GPU and a CPU.
 * @see tests/surface/api/graph/plumbing/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/context_access/context_access.hpp>
#include <SushiRuntime/core/clock.hpp>
#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

/** @brief Measures what the partitioner gives the three graphs of the H9 acceptance on the first machine. */
namespace PartitionHardwareTests
{
    namespace
    {
        /** @brief Elements of every buffer of the independent and the layered graph. */
        constexpr std::size_t K_NODE_ELEMENTS = std::size_t{8} * 1024 * 1024;

        /** @brief Equal nodes of the independent graph. */
        constexpr std::size_t K_INDEPENDENT_NODES = 64;

        /** @brief Nodes per layer of the layered graph. */
        constexpr std::size_t K_LAYER_WIDTH = 16;

        /** @brief Layers of the layered graph. */
        constexpr std::size_t K_LAYERS = 4;

        /** @brief Multiply-add rounds one element goes through in the independent and the layered graph. */
        constexpr std::uint32_t K_MIX_ROUNDS = 64;

        /** @brief Nodes of the chain. */
        constexpr std::size_t K_CHAIN_NODES = 24;

        /** @brief Elements of the small buffer that links one chain node to the next. */
        constexpr std::size_t K_LINK_ELEMENTS = 1024;

        /** @brief The share of the CUDA GPU's memory the chain's carried buffers hold in all, in tenths. */
        constexpr std::size_t K_CHAIN_TENTHS_OF_CAPACITY = 16;

        /** @brief Granule the chain's per-node buffer is rounded down to, in elements. */
        constexpr std::size_t K_CHAIN_GRANULE = std::size_t{1} << 16;

        /** @brief Elements at the start of a buffer the test reads back to check the bytes. */
        constexpr std::size_t K_CHECKED_ELEMENTS = 1024;

        /** @brief Distances from the planned cut, in nodes, at which the chain is cut by hand, each way. */
        constexpr std::size_t K_CUT_DISTANCES[] = {1, 2, 4};

        /** @brief The multiplier the kernels spread their elements with. */
        constexpr std::uint32_t K_SPREAD = 2654435761u;

        /**
         * @brief The ratio of the H9 acceptance: the step with every device is at most this many times the
         *        step on the CUDA GPU alone, and the planned chain is at most this many times its best forced cut.
         * @see docs/agent/2026_11_02_GLOBAL_HANDLE/SPEC.md, row H9.
         */
        constexpr double K_ALLOWED_RATIO = 1.05;

        /** @brief The operation kind the independent graph's nodes state. */
        constexpr const char K_INDEPENDENT_KIND[] = "h9_independent";

        /** @brief The operation kind the layered graph's nodes state. */
        constexpr const char K_LAYERED_KIND[] = "h9_layered";

        /** @brief The operation kind the chain's nodes state. */
        constexpr const char K_CHAIN_KIND[] = "h9_chain";

        /** @brief Returns whether a backend is the CUDA backend. */
        bool is_cuda(sycl::backend backend) noexcept
        {
#if defined(SYCL_IMPLEMENTATION_ACPP)
            return backend == sycl::backend::cuda;
#else
            return backend == sycl::backend::ext_oneapi_cuda;
#endif
        }

        /** @brief Returns whether a backend is the HIP backend. */
        bool is_hip(sycl::backend backend) noexcept
        {
#if defined(SYCL_IMPLEMENTATION_ACPP)
            return backend == sycl::backend::hip;
#else
            return backend == sycl::backend::ext_oneapi_hip;
#endif
        }

        /** @brief Returns the index of the first GPU whose backend @p wanted accepts. */
        template <class Wanted>
        std::optional<std::size_t> first_gpu(const std::vector<DeviceDescription>& devices, Wanted wanted)
        {
            for (const DeviceDescription& device : devices)
                if (device.kind == DeviceKind::GPU && wanted(device.backend))
                    return device.index;
            return std::nullopt;
        }

        /** @brief Returns the index of the first device of kind CPU. */
        std::optional<std::size_t> first_cpu(const std::vector<DeviceDescription>& devices)
        {
            for (const DeviceDescription& device : devices)
                if (device.kind == DeviceKind::CPU)
                    return device.index;
            return std::nullopt;
        }

        /** @brief Returns the description of the device a key names, or null. */
        const DeviceDescription* describe(const std::vector<DeviceDescription>& devices, const DeviceKey& key)
        {
            for (const DeviceDescription& device : devices)
                if (device.key == key)
                    return &device;
            return nullptr;
        }

        /** @brief Returns the value spread over the bits: the arithmetic every kernel of the graphs runs. */
        inline std::uint32_t mix(std::uint32_t value, std::uint32_t salt) noexcept
        {
            for (std::uint32_t round = 0; round < K_MIX_ROUNDS; ++round)
                value = value * K_SPREAD + salt;
            return value;
        }

        /** @brief Returns the elements 0, 1, 2 ... spread, the values the input buffers hold. */
        std::vector<std::uint32_t> spread_values(std::size_t count)
        {
            std::vector<std::uint32_t> values(count);
            for (std::size_t index = 0; index < count; ++index)
                values[index] = static_cast<std::uint32_t>(index) * K_SPREAD;
            return values;
        }

        /** @brief Returns the cost a node of an operation kind states. */
        NodeOptions costed(const char* kind_name, std::size_t kind_length, std::uint64_t operations,
                           std::uint64_t read_bytes, std::uint64_t written_bytes)
        {
            NodeOptions options;
            options.cost = NodeCost{operations, read_bytes, written_bytes,
                                    OperationKind::named(kind_name, kind_length), NodeDType::Unstated};
            return options;
        }

        /** @brief Returns the text of one difference between the bytes read and the bytes expected, or empty. */
        std::string difference(const std::string& name, const Result<std::vector<std::uint32_t>>& actual,
                               const std::vector<std::uint32_t>& expected)
        {
            if (!actual)
                return name + ": " + actual.error().message;
            if (actual.value() != expected)
                return name + ": the bytes differ from the host computation";
            return {};
        }

        /** @brief Prints one number in the form REPORT.md takes: `[h9] name value unit`. */
        void print_number(const std::string& name, double value, const char* unit)
        {
            std::cout << "[h9] " << name << ' ' << std::setprecision(9) << value << ' ' << unit << std::endl;
        }

        /** @brief Returns a name with every space turned into an underscore, so a line stays three fields. */
        std::string one_word(std::string text)
        {
            std::replace(text.begin(), text.end(), ' ', '_');
            return text;
        }

        /** @brief Says where the buffers of a graph live: planned, or on stated devices with one cut. */
        struct Layout
        {
            std::optional<std::size_t> head;                                 /**< Device of the nodes before the cut; none is planned. */
            std::optional<std::size_t> tail;                                 /**< Device of the nodes from the cut on. */
            std::size_t cut = std::numeric_limits<std::size_t>::max();       /**< First node that lives on the tail device. */

            /** @brief Returns the layout that leaves every buffer to the runtime. */
            static Layout planned()
            {
                return Layout{};
            }

            /** @brief Returns the layout that states one device for every buffer. */
            static Layout on(std::size_t device)
            {
                Layout layout;
                layout.head = device;
                layout.tail = device;
                return layout;
            }

            /** @brief Returns the layout whose nodes below @p first_tail live on @p head and the others on @p tail. */
            static Layout split(std::size_t head_device, std::size_t tail_device, std::size_t first_tail)
            {
                Layout layout;
                layout.head = head_device;
                layout.tail = tail_device;
                layout.cut = first_tail;
                return layout;
            }

            /** @brief Returns the device the buffers of a node live on, or none when they are planned. */
            [[nodiscard]] std::optional<std::size_t> at(std::size_t node) const
            {
                return node < cut ? head : tail;
            }
        };

        /** @brief Owns a recorded graph and the buffers it names; each kind of graph is one subclass. */
        class Workload
        {
            public:
                /** @brief Starts an empty graph on a runtime. */
                explicit Workload(const Runtime& runtime) : runtime_(runtime), world_(runtime.dynamic_graph())
                {
                }

                virtual ~Workload() = default;
                Workload(const Workload&) = delete;
                Workload& operator=(const Workload&) = delete;

                /** @brief Returns the graph. */
                DynamicGraph& world() noexcept
                {
                    return world_;
                }

                /** @brief Returns what stopped the graph from being built; empty when it was built. */
                [[nodiscard]] const std::string& failure() const noexcept
                {
                    return failure_;
                }

                /** @brief Returns the first difference between the bytes the graph left and the host computation. */
                [[nodiscard]] virtual std::string mismatch() const = 0;

            protected:
                /** @brief Makes a buffer, planned or on a stated device; null with the failure noted when it cannot be made. */
                Buffer<std::uint32_t>* make_buffer(std::size_t count, std::optional<std::size_t> home)
                {
                    if (!failure_.empty())
                        return nullptr;
                    Result<Buffer<std::uint32_t>> made =
                        home ? runtime_.buffer<std::uint32_t>(count, DeviceIndex{*home}, Residency::Device)
                             : runtime_.buffer<std::uint32_t>(count, Residency::Planned);
                    if (!made)
                    {
                        failure_ = "a buffer of " + std::to_string(count * sizeof(std::uint32_t)) +
                                   " bytes was not made: " + made.error().message;
                        return nullptr;
                    }
                    buffers_.push_back(std::move(made).value());
                    return &buffers_.back();
                }

                /** @brief Makes a buffer and fills it with @p values. */
                Buffer<std::uint32_t>* make_filled(std::optional<std::size_t> home, const std::vector<std::uint32_t>& values)
                {
                    Buffer<std::uint32_t>* made = make_buffer(values.size(), home);
                    if (made == nullptr)
                        return nullptr;
                    const Result<void> written = made->write_range({0, values.size()}, values);
                    if (!written)
                    {
                        failure_ = "a buffer was not filled: " + written.error().message;
                        return nullptr;
                    }
                    return made;
                }

                /** @brief Returns the first @p count elements of a buffer. */
                static Result<std::vector<std::uint32_t>> head_of(const Buffer<std::uint32_t>& buffer, std::size_t count)
                {
                    return buffer.read_range({0, count});
                }

            private:
                const Runtime& runtime_;                         /**< The runtime that owns the buffers. */
                DynamicGraph world_;                             /**< The recorded graph. */
                std::deque<Buffer<std::uint32_t>> buffers_;      /**< The buffers, at addresses that stay put. */
                std::string failure_;                            /**< What stopped the build; empty when nothing did. */
        };

        /** @brief Many equal nodes that read one buffer and write one each. */
        class IndependentWorkload final : public Workload
        {
            public:
                /** @brief Records the graph with its buffers placed by @p layout. */
                IndependentWorkload(const Runtime& runtime, const Layout& layout) : Workload(runtime)
                {
                    const std::optional<std::size_t> home = layout.at(0);
                    input_ = make_filled(home, spread_values(K_NODE_ELEMENTS));
                    for (std::size_t node = 0; node < K_INDEPENDENT_NODES && input_ != nullptr; ++node)
                    {
                        Buffer<std::uint32_t>* output = make_buffer(K_NODE_ELEMENTS, home);
                        if (output == nullptr)
                            return;
                        outputs_.push_back(output);
                        const std::uint32_t salt = salt_of(node);
                        world().region(0).add(K_NODE_ELEMENTS, In(*input_), Out(*output),
                                              [salt](std::size_t index, const std::uint32_t* source, std::uint32_t* target)
                                              { target[index] = mix(source[index], salt); },
                                              costed(K_INDEPENDENT_KIND, sizeof(K_INDEPENDENT_KIND) - 1,
                                                     std::uint64_t{K_NODE_ELEMENTS} * K_MIX_ROUNDS,
                                                     K_NODE_ELEMENTS * sizeof(std::uint32_t),
                                                     K_NODE_ELEMENTS * sizeof(std::uint32_t)));
                    }
                }

                /** @brief Returns the first difference in the first and the last output. */
                [[nodiscard]] std::string mismatch() const override
                {
                    const std::vector<std::uint32_t> source = spread_values(K_CHECKED_ELEMENTS);
                    for (const std::size_t node : {std::size_t{0}, K_INDEPENDENT_NODES - 1})
                    {
                        std::vector<std::uint32_t> expected(K_CHECKED_ELEMENTS);
                        for (std::size_t index = 0; index < K_CHECKED_ELEMENTS; ++index)
                            expected[index] = mix(source[index], salt_of(node));
                        const std::string found =
                            difference("independent output " + std::to_string(node), head_of(*outputs_[node], K_CHECKED_ELEMENTS),
                                       expected);
                        if (!found.empty())
                            return found;
                    }
                    return {};
                }

            private:
                /** @brief Returns the salt of a node. */
                static std::uint32_t salt_of(std::size_t node) noexcept
                {
                    return static_cast<std::uint32_t>(node + 1);
                }

                Buffer<std::uint32_t>* input_ = nullptr;         /**< The buffer every node reads. */
                std::vector<Buffer<std::uint32_t>*> outputs_;    /**< The buffer each node writes, in node order. */
        };

        /** @brief Layers of nodes, each reading two neighbours of the layer before it. */
        class LayeredWorkload final : public Workload
        {
            public:
                /** @brief Records the graph with its buffers placed by @p layout. */
                LayeredWorkload(const Runtime& runtime, const Layout& layout) : Workload(runtime)
                {
                    const std::optional<std::size_t> home = layout.at(0);
                    first_ = make_filled(home, spread_values(K_NODE_ELEMENTS));
                    second_ = make_filled(home, spread_values(K_NODE_ELEMENTS));
                    for (std::size_t layer = 0; layer < K_LAYERS && second_ != nullptr; ++layer)
                    {
                        for (std::size_t column = 0; column < K_LAYER_WIDTH; ++column)
                        {
                            Buffer<std::uint32_t>* output = make_buffer(K_NODE_ELEMENTS, home);
                            if (output == nullptr)
                                return;
                            outputs_.push_back(output);
                            const Buffer<std::uint32_t>& left = layer == 0 ? *first_ : *outputs_[(layer - 1) * K_LAYER_WIDTH + column];
                            const Buffer<std::uint32_t>& right =
                                layer == 0 ? *second_ : *outputs_[(layer - 1) * K_LAYER_WIDTH + (column + 1) % K_LAYER_WIDTH];
                            const std::uint32_t salt = salt_of(layer, column);
                            world().region(0).add(K_NODE_ELEMENTS, In(left), In(right), Out(*output),
                                                  [salt](std::size_t index, const std::uint32_t* first,
                                                         const std::uint32_t* second, std::uint32_t* target)
                                                  { target[index] = mix(first[index] + second[index], salt); },
                                                  costed(K_LAYERED_KIND, sizeof(K_LAYERED_KIND) - 1,
                                                         std::uint64_t{K_NODE_ELEMENTS} * K_MIX_ROUNDS,
                                                         2 * K_NODE_ELEMENTS * sizeof(std::uint32_t),
                                                         K_NODE_ELEMENTS * sizeof(std::uint32_t)));
                        }
                    }
                }

                /** @brief Returns the first difference in the nodes of the last layer. */
                [[nodiscard]] std::string mismatch() const override
                {
                    const std::vector<std::uint32_t> source = spread_values(K_CHECKED_ELEMENTS);
                    std::vector<std::vector<std::uint32_t>> level(K_LAYER_WIDTH, source);
                    std::vector<std::vector<std::uint32_t>> previous = level;
                    for (std::size_t layer = 0; layer < K_LAYERS; ++layer)
                    {
                        for (std::size_t column = 0; column < K_LAYER_WIDTH; ++column)
                        {
                            const std::vector<std::uint32_t>& left = previous[column];
                            const std::vector<std::uint32_t>& right = layer == 0 ? previous[column] : previous[(column + 1) % K_LAYER_WIDTH];
                            for (std::size_t index = 0; index < K_CHECKED_ELEMENTS; ++index)
                                level[column][index] = mix(left[index] + right[index], salt_of(layer, column));
                        }
                        previous = level;
                    }
                    for (std::size_t column = 0; column < K_LAYER_WIDTH; ++column)
                    {
                        const std::string found = difference(
                            "layered output " + std::to_string(column),
                            head_of(*outputs_[(K_LAYERS - 1) * K_LAYER_WIDTH + column], K_CHECKED_ELEMENTS), level[column]);
                        if (!found.empty())
                            return found;
                    }
                    return {};
                }

            private:
                /** @brief Returns the salt of a node. */
                static std::uint32_t salt_of(std::size_t layer, std::size_t column) noexcept
                {
                    return static_cast<std::uint32_t>(layer * K_LAYER_WIDTH + column + 1);
                }

                Buffer<std::uint32_t>* first_ = nullptr;         /**< The first input of the first layer. */
                Buffer<std::uint32_t>* second_ = nullptr;        /**< The second input of the first layer. */
                std::vector<Buffer<std::uint32_t>*> outputs_;    /**< The buffer each node writes, layer by layer. */
        };

        /**
         * @brief Nodes that each read the link the node before wrote, write the next link and write a big
         *        buffer of their own that no later node reads, so every big buffer stays live through the step.
         */
        class ChainWorkload final : public Workload
        {
            public:
                /** @brief Returns the elements of a big buffer, so all of them hold more than the CUDA GPU's memory. */
                static std::size_t big_elements(std::uint64_t cuda_memory_bytes) noexcept
                {
                    const std::uint64_t total = cuda_memory_bytes / 10 * K_CHAIN_TENTHS_OF_CAPACITY;
                    const std::size_t each = static_cast<std::size_t>(total / K_CHAIN_NODES / sizeof(std::uint32_t));
                    return each / K_CHAIN_GRANULE * K_CHAIN_GRANULE;
                }

                /** @brief Records the graph with its buffers placed by @p layout. */
                ChainWorkload(const Runtime& runtime, const Layout& layout, std::size_t big_elements_per_node)
                    : Workload(runtime)
                {
                    Buffer<std::uint32_t>* link = make_filled(layout.at(0), spread_over_links());
                    for (std::size_t node = 0; node < K_CHAIN_NODES && link != nullptr; ++node)
                    {
                        Buffer<std::uint32_t>* next = make_buffer(K_LINK_ELEMENTS, layout.at(node));
                        Buffer<std::uint32_t>* big = make_buffer(big_elements_per_node, layout.at(node));
                        if (next == nullptr || big == nullptr)
                            return;
                        world().region(0).add(big_elements_per_node, In(*link), Out(*next), Out(*big),
                                              [](std::size_t index, const std::uint32_t* previous, std::uint32_t* following,
                                                 std::uint32_t* held)
                                              {
                                                  held[index] = previous[index % K_LINK_ELEMENTS] + static_cast<std::uint32_t>(index);
                                                  if (index < K_LINK_ELEMENTS)
                                                      following[index] = previous[index] + 1u;
                                              },
                                              costed(K_CHAIN_KIND, sizeof(K_CHAIN_KIND) - 1, big_elements_per_node,
                                                     K_LINK_ELEMENTS * sizeof(std::uint32_t),
                                                     (big_elements_per_node + K_LINK_ELEMENTS) * sizeof(std::uint32_t)));
                        last_link_ = next;
                        last_big_ = big;
                        link = next;
                    }
                }

                /** @brief Returns the first difference in the last link and the start of the last big buffer. */
                [[nodiscard]] std::string mismatch() const override
                {
                    const std::vector<std::uint32_t> first = spread_over_links();
                    std::vector<std::uint32_t> link(K_LINK_ELEMENTS);
                    std::vector<std::uint32_t> big(K_CHECKED_ELEMENTS);
                    for (std::size_t index = 0; index < K_LINK_ELEMENTS; ++index)
                        link[index] = first[index] + static_cast<std::uint32_t>(K_CHAIN_NODES);
                    for (std::size_t index = 0; index < K_CHECKED_ELEMENTS; ++index)
                        big[index] = first[index % K_LINK_ELEMENTS] + static_cast<std::uint32_t>(K_CHAIN_NODES - 1 + index);
                    const std::string link_found = difference("chain last link", head_of(*last_link_, K_LINK_ELEMENTS), link);
                    if (!link_found.empty())
                        return link_found;
                    return difference("chain last big buffer", head_of(*last_big_, K_CHECKED_ELEMENTS), big);
                }

            private:
                /** @brief Returns the values the first link holds: 0, 1, 2 ... */
                static std::vector<std::uint32_t> spread_over_links()
                {
                    std::vector<std::uint32_t> values(K_LINK_ELEMENTS);
                    for (std::size_t index = 0; index < K_LINK_ELEMENTS; ++index)
                        values[index] = static_cast<std::uint32_t>(index);
                    return values;
                }

                Buffer<std::uint32_t>* last_link_ = nullptr;     /**< The link the last node writes. */
                Buffer<std::uint32_t>* last_big_ = nullptr;      /**< The big buffer the last node writes. */
        };

        /** @brief How many steps a measurement runs, and how many times it repeats the timed run. */
        struct Schedule
        {
            std::size_t warm_steps;   /**< Steps run first, untimed: they plan, allocate and copy for the first time. */
            std::size_t steps;        /**< Steps in one timed run. */
            std::size_t repeats;      /**< Timed runs; the shortest is kept. */
        };

        /** @brief What one measurement gave. */
        struct Outcome
        {
            double step_seconds = 0.0;   /**< Seconds of one step in the shortest timed run, on the engine's clock. */
            PartitionReport report;      /**< The partition report right after the runs. */
            std::string failure;         /**< What stopped the measurement; empty when nothing did. */
        };

        /** @brief Returns the clock the engine of a runtime times its runs against. */
        const Clock& engine_clock(const Runtime& runtime)
        {
            return Internal::ContextAccess::context(runtime).execution_engine().clock();
        }

        /** @brief Runs a built graph on the schedule, keeps the shortest timed run and checks the bytes. */
        Outcome measure(const Runtime& runtime, Workload& workload, const Schedule& schedule)
        {
            Outcome outcome;
            outcome.failure = workload.failure();
            if (!outcome.failure.empty())
                return outcome;
            const Clock& clock = engine_clock(runtime);
            const auto warmed = workload.world().run(schedule.warm_steps);
            if (!warmed)
            {
                outcome.failure = "warm-up run: " + warmed.error().message;
                return outcome;
            }
            outcome.step_seconds = std::numeric_limits<double>::infinity();
            for (std::size_t repeat = 0; repeat < schedule.repeats; ++repeat)
            {
                const std::uint64_t begin = clock.now_ns();
                const auto stepped = workload.world().run(schedule.steps);
                const std::uint64_t end = clock.now_ns();
                if (!stepped)
                {
                    outcome.failure = "timed run: " + stepped.error().message;
                    return outcome;
                }
                const double seconds = static_cast<double>(end - begin) * 1e-9 / static_cast<double>(schedule.steps);
                outcome.step_seconds = std::min(outcome.step_seconds, seconds);
            }
            outcome.report = runtime.partition_report();
            outcome.failure = workload.mismatch();
            return outcome;
        }

        /** @brief Prints the step of a measurement and, when its graph was planned, every row of its report. */
        void print_outcome(const std::string& name, const Outcome& outcome, const std::vector<DeviceDescription>& devices,
                           bool planned)
        {
            print_number(name + ".step", outcome.step_seconds, "s");
            const PartitionReport& report = outcome.report;
            if (!planned || report.rows.empty())
                return;
            print_number(name + ".modelled_step", report.step_seconds, "s");
            print_number(name + ".ledger_step", report.measured_step_seconds, "s");
            print_number(name + ".floor_modelled_step", report.floor_step_seconds, "s");
            print_number(name + ".floor_taken", report.floor_taken ? 1.0 : 0.0, "flag");
            print_number(name + ".floor_devices", static_cast<double>(report.floor_devices.size()), "devices");
            for (const PartitionDeviceRow& row : report.rows)
            {
                const DeviceDescription* device = describe(devices, row.device);
                const std::string prefix = name + ".device" + (device != nullptr ? std::to_string(device->index) : "?");
                print_number(prefix + ".nodes", static_cast<double>(row.nodes), "nodes");
                print_number(prefix + ".bytes", static_cast<double>(row.bytes), "bytes");
                print_number(prefix + ".modelled", row.modelled_seconds, "s");
                print_number(prefix + ".measured", row.measured_seconds, "s");
            }
        }

        /** @brief Returns whether the plan of a report gives the CPU a node. */
        bool cpu_holds_a_node(const PartitionReport& report, const std::vector<DeviceDescription>& devices)
        {
            for (const PartitionDeviceRow& row : report.rows)
            {
                const DeviceDescription* device = describe(devices, row.device);
                if (device != nullptr && device->kind == DeviceKind::CPU && row.nodes > 0)
                    return true;
            }
            return false;
        }

        /** @brief Where a planned chain was cut: the nodes on the CUDA GPU and the one other device that holds the rest. */
        struct PlannedCut
        {
            std::string failure;        /**< Why the plan is not a cut between two devices; empty when it is. */
            std::size_t cut = 0;        /**< Nodes on the CUDA GPU, the first of the chain. */
            std::size_t other = 0;      /**< Index of the other device. */
        };

        /** @brief Reads the cut of a planned chain from its report. */
        PlannedCut read_cut(const PartitionReport& report, const std::vector<DeviceDescription>& devices, std::size_t cuda)
        {
            PlannedCut found;
            std::size_t holders = 0;
            std::size_t placed = 0;
            bool cuda_holds = false;
            for (const PartitionDeviceRow& row : report.rows)
            {
                const DeviceDescription* device = describe(devices, row.device);
                if (row.nodes == 0 || device == nullptr)
                    continue;
                ++holders;
                placed += row.nodes;
                if (device->index == cuda)
                {
                    cuda_holds = true;
                    found.cut = row.nodes;
                }
                else
                    found.other = device->index;
            }
            if (!cuda_holds || holders != 2 || placed != K_CHAIN_NODES)
                found.failure = "the planned chain is not cut between the CUDA GPU and one other device: " +
                                std::to_string(holders) + " devices hold " + std::to_string(placed) + " of " +
                                std::to_string(K_CHAIN_NODES) + " nodes, and the CUDA GPU holds " +
                                (cuda_holds ? "some" : "none");
            return found;
        }

        /** @brief The shortest step among the forced cuts that were measured. */
        struct ForcedCuts
        {
            double best_step_seconds = std::numeric_limits<double>::infinity(); /**< The best step among them. */
            std::size_t measured = 0;                                           /**< Forced cuts that fitted and ran. */
            std::string failure;                                                /**< The first failure of a forced cut that fitted. */
        };
    } // namespace

    /** @brief Fixture that makes one runtime with a profile file for the suite and skips without the hardware. */
    class Large_PartitionHardware : public ::testing::Test
    {
        protected:
            /** @brief Makes the runtime and finds the three devices; notes what is missing. */
            static void SetUpTestSuite()
            {
                directory_ = std::filesystem::temp_directory_path() / "sushiruntime_partition_hardware";
                std::filesystem::remove_all(directory_);
                std::filesystem::create_directories(directory_);
                RuntimeConfig config;
                config.device_profile_file = (directory_ / "device_profile.txt").string();
                Result<Runtime> created = Runtime::create(config);
                if (!created)
                {
                    missing_ = "the runtime could not be created: " + created.error().message;
                    return;
                }
                runtime_.emplace(std::move(created).value());
                const std::vector<DeviceDescription>& devices = runtime_->advanced().devices();
                const std::optional<std::size_t> cuda = first_gpu(devices, is_cuda);
                const std::optional<std::size_t> hip = first_gpu(devices, is_hip);
                const std::optional<std::size_t> cpu = first_cpu(devices);
                if (!cuda)
                    missing_ += "the runtime drives no CUDA GPU; the 3080 Ti is the device every graph is compared with. ";
                if (!hip)
                    missing_ += "the runtime drives no HIP GPU; the integrated GPU is the second device of the plan. ";
                if (!cpu)
                    missing_ += "the runtime drives no CPU device; the CPU is the third device of the plan. ";
                if (cuda && hip && cpu)
                {
                    cuda_ = *cuda;
                    announce_devices();
                }
            }

            /** @brief Releases the runtime and removes the profile file. */
            static void TearDownTestSuite()
            {
                runtime_.reset();
                std::error_code ignored;
                std::filesystem::remove_all(directory_, ignored);
            }

            /** @brief Skips the test, naming what the machine lacks. */
            void SetUp() override
            {
                if (!missing_.empty())
                    GTEST_SKIP() << missing_;
            }

            /** @brief Builds, runs and prints the planned graph and the same graph on the CUDA GPU alone. */
            static void measure_against_cuda_alone(const std::string& name,
                                                   const std::function<std::unique_ptr<Workload>(const Runtime&, const Layout&)>& make)
            {
                const std::vector<DeviceDescription>& devices = runtime_->advanced().devices();
                {
                    const std::unique_ptr<Workload> planned = make(*runtime_, Layout::planned());
                    every_ = measure(*runtime_, *planned, K_THROUGHPUT_SCHEDULE);
                    print_outcome(name + ".every_device", every_, devices, true);
                }
                {
                    const std::unique_ptr<Workload> alone = make(*runtime_, Layout::on(cuda_));
                    alone_ = measure(*runtime_, *alone, K_THROUGHPUT_SCHEDULE);
                    print_outcome(name + ".cuda_alone", alone_, devices, false);
                }
                cpu_holds_ = cpu_holds_a_node(every_.report, devices);
                print_number(name + ".cpu_holds_a_node", cpu_holds_ ? 1.0 : 0.0, "flag");
            }

            /** @brief Asserts the two sentences of H9 for the two graphs that are compared with the CUDA GPU alone. */
            static void expect_h9_against_cuda_alone()
            {
                ASSERT_TRUE(every_.failure.empty()) << every_.failure;
                ASSERT_TRUE(alone_.failure.empty()) << alone_.failure;
                ASSERT_FALSE(every_.report.refused) << every_.report.refusal;
                EXPECT_LE(every_.step_seconds, K_ALLOWED_RATIO * alone_.step_seconds)
                    << every_.report.to_text();
                if (cpu_holds_)
                    EXPECT_LE(every_.step_seconds, alone_.step_seconds) << every_.report.to_text();
            }

            /** @brief Measures the chain with its buffers placed by @p layout and prints the outcome. */
            static Outcome measure_chain(const std::string& name, const Layout& layout, bool planned)
            {
                const std::size_t elements = ChainWorkload::big_elements(runtime_->advanced().devices()[cuda_].memory_bytes);
                const std::unique_ptr<Workload> chain = std::make_unique<ChainWorkload>(*runtime_, layout, elements);
                Outcome outcome = measure(*runtime_, *chain, K_CHAIN_SCHEDULE);
                print_outcome(name, outcome, runtime_->advanced().devices(), planned);
                return outcome;
            }

            /** @brief Measures the chain cut by hand at every distance from the planned cut, each way; none without a planned cut. */
            static ForcedCuts measure_forced_cuts(const PlannedCut& planned)
            {
                ForcedCuts forced;
                if (!planned.failure.empty())
                    return forced;
                for (const std::size_t distance : K_CUT_DISTANCES)
                {
                    measure_forced_cut(planned, planned.cut - std::min(planned.cut, distance), forced);
                    measure_forced_cut(planned, planned.cut + distance, forced);
                }
                return forced;
            }

            /** @brief Asserts the sentence of H9 for the chain: the planned step is at most 1.05 times the best forced cut. */
            static void expect_h9_chain(const Outcome& planned, const PlannedCut& cut, const ForcedCuts& forced)
            {
                ASSERT_TRUE(planned.failure.empty()) << planned.failure;
                ASSERT_FALSE(planned.report.refused) << planned.report.refusal;
                ASSERT_TRUE(cut.failure.empty()) << cut.failure << '\n' << planned.report.to_text();
                ASSERT_TRUE(forced.failure.empty()) << forced.failure;
                ASSERT_GT(forced.measured, 0u) << "no forced cut fitted the memory of its devices";
                EXPECT_LE(planned.step_seconds, K_ALLOWED_RATIO * forced.best_step_seconds) << planned.report.to_text();
            }

            /** @brief Returns the index of the CUDA GPU. */
            static std::size_t cuda_device() noexcept
            {
                return cuda_;
            }

            /** @brief Returns the devices the runtime drives. */
            static const std::vector<DeviceDescription>& devices()
            {
                return runtime_->advanced().devices();
            }

        private:
            /** @brief Prints which device each index of the other lines is. */
            static void announce_devices()
            {
                for (const DeviceDescription& device : runtime_->advanced().devices())
                    print_number("device." + std::to_string(device.index) + "." + one_word(device.name),
                                 static_cast<double>(device.memory_bytes), "bytes");
            }

            /** @brief Measures one forced cut and folds it into @p forced; a cut that does not fit its devices is printed and skipped. */
            static void measure_forced_cut(const PlannedCut& planned, std::size_t first_tail, ForcedCuts& forced)
            {
                if (first_tail < 1 || first_tail >= K_CHAIN_NODES)
                    return;
                const std::string name = "chain.forced_cut_" + std::to_string(first_tail);
                const std::size_t elements = ChainWorkload::big_elements(devices()[cuda_].memory_bytes);
                const std::unique_ptr<Workload> chain = std::make_unique<ChainWorkload>(
                    *runtime_, Layout::split(cuda_, planned.other, first_tail), elements);
                if (!chain->failure().empty())
                {
                    print_number(name + ".fits", 0.0, "flag");
                    return;
                }
                const Outcome outcome = measure(*runtime_, *chain, K_CHAIN_SCHEDULE);
                print_outcome(name, outcome, devices(), false);
                if (!outcome.failure.empty())
                {
                    if (forced.failure.empty())
                        forced.failure = name + ": " + outcome.failure;
                    return;
                }
                ++forced.measured;
                forced.best_step_seconds = std::min(forced.best_step_seconds, outcome.step_seconds);
            }

            /** @brief Schedule of the independent and the layered graph: short steps, many of them, best of seven. */
            static constexpr Schedule K_THROUGHPUT_SCHEDULE{4, 32, 7};

            /** @brief Schedule of the chain: long steps, few of them, best of three. */
            static constexpr Schedule K_CHAIN_SCHEDULE{1, 6, 3};

            inline static std::filesystem::path directory_;    /**< The suite's folder; holds the profile file. */
            inline static std::optional<Runtime> runtime_;     /**< The runtime every test of the suite measures on. */
            inline static std::string missing_;                /**< What the machine lacks; empty when nothing. */
            inline static std::size_t cuda_ = 0;               /**< Index of the CUDA GPU. */
            inline static Outcome every_;                      /**< The last planned graph, every device. */
            inline static Outcome alone_;                      /**< The same graph on the CUDA GPU alone. */
            inline static bool cpu_holds_ = false;             /**< Whether the CPU held a node of the last planned graph. */
    };

    /**
     * @brief Tests the first sentence of the H9 acceptance for a set of independent equal nodes: the step
     *        with every device is at most 1.05 times the step on the CUDA GPU alone, and at most that step
     *        when the CPU holds a node.
     */
    TEST_F(Large_PartitionHardware, IndependentEqualNodesWithEveryDeviceStepNoLongerThanOnTheCUDAGPUAlone)
    {
        measure_against_cuda_alone("independent", [](const Runtime& runtime, const Layout& layout)
                                   { return std::make_unique<IndependentWorkload>(runtime, layout); });

        expect_h9_against_cuda_alone();
    }

    /**
     * @brief Tests the same two bounds for a graph of layers that fits on the CUDA GPU.
     */
    TEST_F(Large_PartitionHardware, ALayeredGraphThatFitsWithEveryDeviceStepNoLongerThanOnTheCUDAGPUAlone)
    {
        measure_against_cuda_alone("layered", [](const Runtime& runtime, const Layout& layout)
                                   { return std::make_unique<LayeredWorkload>(runtime, layout); });

        expect_h9_against_cuda_alone();
    }

    /**
     * @brief Tests the second sentence of the H9 acceptance for a chain whose carried buffers exceed the CUDA
     *        GPU's memory: the planned step is at most 1.05 times the best of the cuts forced one, two and four
     *        nodes to either side of the planned cut.
     */
    TEST_F(Large_PartitionHardware, AChainLargerThanTheCUDAGPUStepsNoLongerThanItsBestForcedCut)
    {
        const Outcome planned = measure_chain("chain.planned", Layout::planned(), true);
        const PlannedCut cut = read_cut(planned.report, devices(), cuda_device());
        const ForcedCuts forced = measure_forced_cuts(cut);
        print_number("chain.planned.cut", static_cast<double>(cut.cut), "nodes");
        print_number("chain.best_forced_step", forced.best_step_seconds, "s");

        expect_h9_chain(planned, cut, forced);
    }
} // namespace PartitionHardwareTests
