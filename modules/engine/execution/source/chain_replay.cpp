/**************************************************************************/
/* chain_replay.cpp                                                       */
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
 * @file chain_replay.cpp
 * @brief Implements the recording of a chained plan and ChainReplay, which replays it.
 * @author Mustafa Garip
 * @see modules/engine/execution/source/chain_replay.md
 */

#include <SushiRuntime/execution/chain_replay.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <utility>
#include <vector>
#include <sycl/sycl.hpp>
#if __has_include(<sycl/ext/oneapi/experimental/graph.hpp>)
#include <sycl/ext/oneapi/experimental/graph.hpp>
#define SR_HAS_SYCL_GRAPH 1
#else
#define SR_HAS_SYCL_GRAPH 0
#endif
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/profile/profile.hpp>
#include <SushiRuntime/sycl/error_translation.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/device/queue_flavour.hpp>
#include <SushiRuntime/task/late_bindings.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/submit_form.hpp>
#include <SushiRuntime/task/task_submission.hpp>
#include <SushiRuntime/execution/execution_log_category.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        namespace
        {
#if SR_HAS_SYCL_GRAPH
            namespace GraphExtension = sycl::ext::oneapi::experimental;
#endif

            using Internal::Graph::LateBindings;
            using Internal::Graph::Node;
            using Internal::Graph::SubmitForm;

#if SR_HAS_SYCL_GRAPH
            /** @brief Names the reason a plan stays on layer 1 after its recording threw. */
            constexpr const char* K_RECORDING_FAILED = "recording or finalizing the command graph threw";

            /** @brief Records into a command graph on a private queue and ends the recording on every path. */
            class RecordingScope
            {
                public:
                    /** @brief Starts recording @p queue into @p graph. */
                    RecordingScope(GraphExtension::command_graph<>& graph, sycl::queue& queue)
                        : graph_(graph), queue_(queue)
                    {
                        graph_.begin_recording(queue_);
                    }

                    /** @brief Ends the recording unless end() already did; a failure is logged. */
                    ~RecordingScope()
                    {
                        if (ended_)
                            return;
                        const Result<void> ended = SYCL::translate_exceptions([this]() { graph_.end_recording(queue_); });
                        if (!ended)
                            SR_LOG_WARN(K_EXECUTION_LOG_CATEGORY,
                                        "Ending a failed recording threw: {}; its private queue is dropped.",
                                        ended.error().message);
                    }

                    RecordingScope(const RecordingScope&) = delete;
                    RecordingScope& operator=(const RecordingScope&) = delete;
                    RecordingScope(RecordingScope&&) = delete;
                    RecordingScope& operator=(RecordingScope&&) = delete;

                    /** @brief Ends the recording; what it throws reaches the caller. */
                    void end()
                    {
                        ended_ = true;
                        graph_.end_recording(queue_);
                    }

                private:
                    GraphExtension::command_graph<>& graph_; /**< The graph recorded into. */
                    sycl::queue& queue_;                     /**< The private queue recorded. */
                    bool ended_ = false;                     /**< Whether end() ran. */
            };

            /**
             * @brief Submits one node to the recording queue.
             *
             * node_refusal() keeps host bodies out of a recording, so the submission cannot return an error.
             */
            void submit_recorded(sycl::queue& recording, Node& node)
            {
                static_cast<void>(Internal::Graph::submit_task(recording, node, nullptr, nullptr).value());
            }

            /** @brief Hands a late-bound node its host words for one submission, and takes them back. */
            class WordsLoan
            {
                public:
                    /** @brief Points @p bindings at @p words. */
                    WordsLoan(LateBindings& bindings, std::uint64_t* words) noexcept : bindings_(bindings)
                    {
                        bindings_.replay_words = words;
                    }

                    /** @brief Leaves the bindings with no words, so layer 1 lowers the node as before. */
                    ~WordsLoan()
                    {
                        bindings_.replay_words = nullptr;
                    }

                    WordsLoan(const WordsLoan&) = delete;
                    WordsLoan& operator=(const WordsLoan&) = delete;
                    WordsLoan(WordsLoan&&) = delete;
                    WordsLoan& operator=(WordsLoan&&) = delete;

                private:
                    LateBindings& bindings_; /**< The node's per-step controls. */
            };

            /**
             * @brief Holds one recorded and finalized step of a chained plan and the host words
             *        its late-bound nodes read.
             * @see docs/design/GRAPH_REPLAY/README.md
             */
            class RecordedChain final : public Internal::Graph::RecordedSteps
            {
                public:
                    /**
                     * @brief Records every node of @p order on a private in-order queue of
                     *        @p queue's device, then finalizes the graph.
                     * @param order Nodes replay_refusal() accepted, in the chain's order.
                     * @throws What allocating, recording or finalizing throws.
                     */
                    RecordedChain(const sycl::queue& queue, const std::vector<Node*>& order)
                        : context_(queue.get_context())
                    {
                        for (const Node* node : order)
                            if (node->submit_form == SubmitForm::LateWindow)
                                windows_.push_back(node->bindings_);

                        if (!windows_.empty())
                        {
                            words_ = sycl::malloc_host<std::uint64_t>(windows_.size() * LateBindings::K_REPLAY_WORDS,
                                                                       context_);
                            if (words_ == nullptr)
                                throw std::bad_alloc();
                        }

                        sycl::queue recording(context_, queue.get_device(),
                                              sycl::property_list{sycl::property::queue::in_order{}});
                        GraphExtension::command_graph<> graph(context_, queue.get_device());
                        RecordingScope scope(graph, recording);
                        std::size_t window = 0;
                        for (Node* node : order)
                        {
                            if (node->submit_form != SubmitForm::LateWindow)
                            {
                                submit_recorded(recording, *node);
                                continue;
                            }
                            const WordsLoan loan(*node->bindings_, words_of(window++));
                            submit_recorded(recording, *node);
                        }
                        scope.end();
                        executable_.emplace(graph.finalize());
                    }

                    /** @brief Frees the host words; no replay of this step is running. */
                    ~RecordedChain() override
                    {
                        if (words_ != nullptr)
                            sycl::free(words_, context_);
                    }

                    /**
                     * @brief Copies each late-bound node's controls into its words, then submits the step.
                     */
                    sycl::event replay(sycl::queue& queue) override
                    {
                        for (std::size_t k = 0; k < windows_.size(); ++k)
                        {
                            const LateBindings& bindings = *windows_[k];
                            std::uint64_t* words = words_of(k);
                            words[LateBindings::K_REPLAY_WORD_ENABLED] =
                                bindings.enabled.load(std::memory_order_relaxed) ? 1u : 0u;
                            words[LateBindings::K_REPLAY_WORD_COUNT] =
                                static_cast<std::uint64_t>(bindings.count.load(std::memory_order_relaxed));
                            words[LateBindings::K_REPLAY_WORD_BASE] =
                                static_cast<std::uint64_t>(bindings.base.load(std::memory_order_relaxed));
                        }
                        return queue.ext_oneapi_graph(*executable_);
                    }

                    /** @brief Returns whether a late-bound node reads host words. */
                    bool reads_host_words() const noexcept override
                    {
                        return !windows_.empty();
                    }

                private:
                    /** @brief Returns the words of the late-bound node at @p window in the chain's order. */
                    std::uint64_t* words_of(std::size_t window) const noexcept
                    {
                        return words_ + window * LateBindings::K_REPLAY_WORDS;
                    }

                    sycl::context context_;                          /**< The context the words are allocated in. */
                    std::vector<std::shared_ptr<LateBindings>> windows_; /**< Controls of each late-bound node, in order. */
                    std::uint64_t* words_ = nullptr;                 /**< K_REPLAY_WORDS host-USM words per window. */
                    std::optional<GraphExtension::command_graph<GraphExtension::graph_state::executable>>
                        executable_;                                 /**< The finalized step. */
            };

#endif // SR_HAS_SYCL_GRAPH

            /** @brief Returns why one chained node cannot be recorded, or null when it can. */
            const char* node_refusal(const Node& node) noexcept
            {
                if (node.execution_mode() == Internal::Scheduler::Task::ExecutionMode::Host)
                    return "a node is a task body, whose submissions a recording is not shown to capture";
                switch (node.submit_form)
                {
                    case SubmitForm::Fixed:
                        return nullptr;
                    case SubmitForm::LateWindow:
                        return (node.bindings_ == nullptr || node.bindings_->device_count != nullptr ||
                                node.bindings_->device_base != nullptr)
                            ? "a late-bound node reads its window from a word a predecessor writes on the device"
                            : nullptr;
                    case SubmitForm::DeviceSized:
                        return "a node is sized from a word a predecessor writes on the device";
                    case SubmitForm::StepBound:
                        break;
                }
                return "a node reads host state at submit that may change between steps, such as a State's fields";
            }

            /**
             * @brief Returns whether layer 2 runs on the backend of @p queue.
             *
             * Without the oneAPI graph extension it returns false and does not ask @p queue for its
             * backend.
             */
            bool queue_replays(const sycl::queue& queue) noexcept
            {
#if SR_HAS_SYCL_GRAPH
                return replays_on(queue.get_backend());
#else
                static_cast<void>(queue);
                return false;
#endif
            }
        } // namespace

        /**
         * @brief Returns why a chained plan's nodes cannot be recorded as one command graph.
         */
        const char* replay_refusal(const Internal::Graph::CompiledGraph& graph) noexcept
        {
            if (!graph.chain.chainable)
                return "the plan does not run as a chain";
            for (const Node* node : graph.chain.order)
                if (const char* refused = node_refusal(*node))
                    return refused;
            return nullptr;
        }

        /**
         * @brief Returns whether layer 2 runs on a backend: CUDA and OpenCL, which the spike replayed on.
         *
         * A SYCL implementation without the oneAPI graph extension replays on none.
         */
        bool replays_on(sycl::backend backend) noexcept
        {
#if SR_HAS_SYCL_GRAPH
            return backend == sycl::backend::ext_oneapi_cuda || backend == sycl::backend::opencl;
#else
            static_cast<void>(backend);
            return false;
#endif
        }

        /**
         * @brief Settles once per plan whether a chained plan replays, recording its step the first time.
         */
        bool prepare_replay(RuntimeContext& context, Internal::Graph::CompiledGraph& graph)
        {
            Internal::Graph::ChainPlan& chain = graph.chain;
            if (chain.recorded != nullptr)
                return true;
            if (chain.not_replayable != nullptr)
                return false;

            sycl::queue& queue = context.devices().queue(chain.queue_index, Internal::Device::QueueFlavour{true, false});
            const char* refusal = replay_refusal(graph);
            if (refusal == nullptr && !queue_replays(queue))
                refusal = "the device's backend has not replayed a recorded graph in the spike or CI";
            if (refusal != nullptr)
            {
                SR_LOG_DEBUG(K_EXECUTION_LOG_CATEGORY, "The plan runs as a chain, not replayed: {}.", refusal);
                chain.not_replayable = refusal;
                return false;
            }

#if SR_HAS_SYCL_GRAPH
            SR_ZONE(context.profile_collector(), "chain.record");
            const Result<void> recorded = SYCL::translate_exceptions([&chain, &queue]()
            {
                chain.recorded = std::make_unique<RecordedChain>(queue, chain.order);
            });
            if (!recorded)
            {
                SR_LOG_WARN(K_EXECUTION_LOG_CATEGORY,
                            "Recording the plan of {} nodes as a command graph failed: {}; it runs as a chain "
                            "until it is compiled or composed again.",
                            chain.order.size(), recorded.error().message);
                chain.not_replayable = K_RECORDING_FAILED;
                return false;
            }
            return true;
#else
            return false;
#endif
        }

        /**
         * @brief Binds the run to the plan's untimed in-order queue and marks it in progress.
         */
        ChainReplay::ChainReplay(RuntimeContext& context, Internal::Graph::CompiledGraph& graph)
            : recorded_(*graph.chain.recorded),
              collector_(context.profile_collector()),
              queue_(context.devices().queue(graph.chain.queue_index, Internal::Device::QueueFlavour{true, false})),
              run_mark_(context.execution_engine())
        {

        }

        /**
         * @brief Drains the queue unless finish() already did, and logs the errors it held.
         */
        ChainReplay::~ChainReplay()
        {
            if (finished_)
                return;

            // wait_and_throw() takes the queue's asynchronous errors, so the next run meets none.
            const Result<void> drained = SYCL::translate_exceptions([this]() { queue_.wait_and_throw(); });
            if (!drained)
                SR_LOG_WARN(K_EXECUTION_LOG_CATEGORY, "Device error while draining an abandoned replay: {}",
                            drained.error().message);
        }

        /**
         * @brief Submits one replay of the recorded step without waiting.
         */
        Result<void> ChainReplay::submit_step()
        {
            SR_ZONE(collector_, "chain.replay_step");
            return SYCL::translate_exceptions([this]()
            {
                // The words of the step in flight are read on the device; they change only after it.
                if (last_)
                    last_->wait_and_throw();
                sycl::event replayed = recorded_.replay(queue_);
                if (recorded_.reads_host_words())
                    last_ = std::move(replayed);
            });
        }

        /**
         * @brief Blocks until every submitted step has finished.
         */
        Result<void> ChainReplay::wait()
        {
            SR_ZONE(collector_, "chain.wait");
            return SYCL::translate_exceptions([this]()
            {
                queue_.wait_and_throw();
                last_.reset();
            });
        }

        /**
         * @brief Waits once for the whole run; the run is over when this returns.
         */
        Result<void> ChainReplay::finish()
        {
            Result<void> waited = wait();
            run_mark_.release();
            if (!waited)
                return waited;
            finished_ = true;
            return {};
        }

    } // namespace Execution
} // namespace SushiRuntime
