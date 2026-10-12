/**************************************************************************/
/* recorder.hpp                                                           */
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
 * @file recorder.hpp
 * @brief Declares Recorder, the owner of the work recorded for a graph or a region.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/api/data/state.hpp>
#include <SushiRuntime/api/detail/callable_checks.hpp>
#include <SushiRuntime/api/detail/callable_traits.hpp>
#include <SushiRuntime/api/detail/device_fault.hpp>
#include <SushiRuntime/api/detail/emit_captured.hpp>
#include <SushiRuntime/api/detail/emit_device_sized.hpp>
#include <SushiRuntime/api/detail/emit_dynamic.hpp>
#include <SushiRuntime/api/detail/emit_host.hpp>
#include <SushiRuntime/api/detail/emit_launch.hpp>
#include <SushiRuntime/api/detail/emit_late_count_resolver.hpp>
#include <SushiRuntime/api/detail/emit_state.hpp>
#include <SushiRuntime/api/detail/emit_task.hpp>
#include <SushiRuntime/api/detail/emit_tracked.hpp>
#include <SushiRuntime/api/detail/emit_untracked.hpp>
#include <SushiRuntime/api/detail/handle_access.hpp>
#include <SushiRuntime/api/detail/late_count_resolution.hpp>
#include <SushiRuntime/api/detail/node_description.hpp>
#include <SushiRuntime/api/detail/node_spec.hpp>
#include <SushiRuntime/api/detail/node_stamp.hpp>
#include <SushiRuntime/api/detail/step_machinery.hpp>
#include <SushiRuntime/api/graph/access.hpp>
#include <SushiRuntime/api/graph/device_sized.hpp>
#include <SushiRuntime/api/graph/node_options.hpp>
#include <SushiRuntime/api/launch/extent.hpp>
#include <SushiRuntime/api/launch/nd_range.hpp>
#include <SushiRuntime/api/runtime/context_handle.hpp>
#include <SushiRuntime/api/vocabulary/coupled.hpp>
#include <SushiRuntime/api/vocabulary/dependencies.hpp>
#include <SushiRuntime/api/vocabulary/dynamic.hpp>
#include <SushiRuntime/api/vocabulary/late_count.hpp>
#include <SushiRuntime/api/vocabulary/placement.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/region/buffer_region.hpp>
#include <SushiRuntime/region/liveness.hpp>
#include <SushiRuntime/report/dispatch_width.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        struct RecorderReplay;
    } // namespace Detail

    /**
     * @brief Owns the nodes recorded for one graph or region and replays them.
     *
     * Only a Graph or a Region starts one or moves one. Every recording member returns
     * the position of its node's rule. A recording it refuses records nothing, is kept
     * for refusal() when it is the first, and returns rule_count().
     * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
     */
    class Recorder final
    {
        public:
            /**
             * @brief Groups every node recorded while it lives into one rule.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            class RuleScope
            {
                public:
                    /** @brief Takes over the rule other holds open. */
                    RuleScope(RuleScope&& other) noexcept : recorder_(other.recorder_)
                    {
                        other.recorder_ = nullptr;
                    }

                    RuleScope(const RuleScope&) = delete;
                    RuleScope& operator=(const RuleScope&) = delete;
                    RuleScope& operator=(RuleScope&&) = delete;

                    /** @brief Closes the rule; a later node is a rule of its own. */
                    ~RuleScope()
                    {
                        if (recorder_ != nullptr)
                            recorder_->scope_open_ = false;
                    }

                private:
                    friend class Recorder;

                    explicit RuleScope(Recorder* recorder) noexcept : recorder_(recorder)
                    {
                    }

                    /** @brief The recorder whose rule this scope closes, or null for none. */
                    Recorder* recorder_ = nullptr;
            };

            Recorder(const Recorder&) = delete;
            Recorder& operator=(const Recorder&) = delete;

            /**
             * @brief Opens a rule: the nodes recorded until the scope ends form one rule.
             * @return The scope; a scope opened inside another joins the outer rule.
             * @pre The scope ends before this recorder is moved, cleared or destroyed.
             */
            [[nodiscard]] RuleScope rule() noexcept
            {
                if (scope_open_)
                    return RuleScope(nullptr);
                scope_open_ = true;
                scope_has_node_ = false;
                return RuleScope(this);
            }

            /**
             * @brief Records a launch over [0, count), ordered by its access.
             * @param kernel_body Callable invoked as kernel_body(i) for each index, or as
             *        kernel_body(0, count, sycl::handler&) to drive the handler itself.
             */
            template <class F>
            std::size_t kernel(std::size_t count, Access access, F&& kernel_body,
                               NodeOptions options = {})
            {
                if constexpr (!Detail::accepts_kernel<std::size_t, F>())
                    return rule_count();
                else if constexpr (Detail::is_range_kernel_v<F>)
                    return record(Detail::range_node(std::move(access.reads),
                                                     std::move(access.writes), count,
                                                     std::forward<F>(kernel_body)),
                                  access.lives, options);
                else
                    return record(Detail::launch_node(std::move(access.reads),
                                                      std::move(access.writes), count,
                                                      std::forward<F>(kernel_body)),
                                  access.lives, options);
            }

            /**
             * @brief Records a launch over an Extent, ordered by its access.
             * @param kernel_body Callable invoked as kernel_body(sycl::id<N>) with the extent's
             *        origin applied.
             */
            template <int N, class F>
            std::size_t kernel(Extent<N> extent, Access access, F&& kernel_body,
                               NodeOptions options = {})
            {
                return record(Detail::launch_node(std::move(access.reads), std::move(access.writes),
                                                  extent, std::forward<F>(kernel_body)),
                              access.lives, options);
            }

            /**
             * @brief Records a tiled launch over an NdRange, ordered by its access.
             * @param kernel_body Callable invoked as kernel_body(sycl::nd_item<N>), or as
             *        kernel_body(sycl::nd_item<N>, sycl::id<N>) to get the id with the origin
             *        applied.
             */
            template <int N, class F>
            std::size_t kernel(NdRange<N> range, Access access, F&& kernel_body,
                               NodeOptions options = {})
            {
                return record(Detail::launch_node(std::move(access.reads), std::move(access.writes),
                                                  range, std::forward<F>(kernel_body)),
                              access.lives, options);
            }

            /**
             * @brief Records a launch run at capacity that reads its live width on the device.
             * @param kernel_body Callable invoked as kernel_body(t), kernel_body(t, w0) or
             *        kernel_body(t, w0, w1); the words come from launch.live_words.
             * @pre access names the regions launch.live_width and launch.live_words lie in.
             */
            template <class F>
            std::size_t kernel(DeviceSized launch, Access access, F&& kernel_body,
                               NodeOptions options = {})
            {
                return record(Detail::device_sized_node(std::move(access.reads),
                                                        std::move(access.writes), launch,
                                                        std::forward<F>(kernel_body),
                                                        options.width),
                              access.lives, options);
            }

            /**
             * @brief Records a per-element launch whose window is bound per step.
             * @param capacity Upper bound the live window must fit; also the default width.
             */
            template <class F, std::enable_if_t<Detail::is_per_element_v<F>, int> = 0>
            std::size_t dynamic_kernel(Dynamic window, std::size_t capacity, Access access,
                                       F&& kernel_body, NodeOptions options = {})
            {
                require_device_slots(window);
                if (!admits(options))
                    return rule_count();
                auto bindings = Detail::make_late_bindings(window, access.reads, access.lives);
                Result<std::uint32_t*> fault = fault_record_for(window);
                if (!admit(fault))
                    return rule_count();
                const std::size_t width =
                    options.width == K_WIDTH_UNKNOWN ? capacity : options.width;

                const std::size_t rule = record(
                    Detail::dynamic_node(
                        bindings, *fault, std::move(access.reads), std::move(access.writes),
                        capacity, std::forward<F>(kernel_body), width),
                    access.lives, options);
                steps_.register_binding_updater(std::move(bindings), std::move(window));
                return rule;
            }

            /**
             * @brief Records a raw command group, ordered by its access.
             * @param kernel_body Callable invoked as kernel_body(sycl::handler&).
             */
            template <class F>
            std::size_t raw_kernel(Access access, F&& kernel_body, NodeOptions options = {})
            {
                return record(Detail::raw_node(std::move(access.reads), std::move(access.writes),
                                               std::forward<F>(kernel_body), options.width),
                              access.lives, options);
            }

            /**
             * @brief Records a launch whose kernel gets one raw pointer per capture.
             * @tparam Launch std::size_t, Extent<N> or NdRange<N>.
             * @param pointers A std::tuple of pointers, passed to kernel_body after the index.
             */
            template <class Launch, class Pointers, class F>
            std::size_t captured_kernel(Launch launch, Access access, Pointers pointers,
                                        F&& kernel_body,
                                        NodeOptions options = {})
            {
                return record(
                    Detail::captured_node(
                        std::move(access.reads), std::move(access.writes), launch,
                        std::move(pointers), std::forward<F>(kernel_body)),
                    access.lives, options);
            }

            /**
             * @brief Records a task that runs once on a worker thread, ordered by its access.
             * @param kernel_body Nullary callable; must not submit SYCL work.
             */
            template <class F>
            std::size_t host(Access access, F&& kernel_body, NodeOptions options = {})
            {
                return record(Detail::host_node(std::move(access.reads), std::move(access.writes),
                                                std::forward<F>(kernel_body), options.width),
                              access.lives, options);
            }

            /**
             * @brief Records a task that submits its own work on the queue it is handed.
             * @param body Callable invoked once per step as
             *        body(sycl::queue&, const std::vector<sycl::event>&); it waits for nothing
             *        and returns the event that ends its work.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            template <class F>
            std::size_t task(Access access, F&& body, NodeOptions options = {})
            {
                return record(Detail::task_node(std::move(access.reads), std::move(access.writes),
                                                std::forward<F>(body), options.width),
                              access.lives, options);
            }

            /**
             * @brief Records a per-element launch over [0, count) that names no region.
             * @param kernel_body        Callable invoked as kernel_body(i) for each index.
             * @param placement Device the node runs on; a handle it names is watched.
             * @param call      Names the call the user wrote in a message.
             * @pre placement names a device of this runtime.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            template <class F, std::enable_if_t<Detail::is_per_element_v<F>, int> = 0>
            std::size_t untracked_kernel(std::size_t count, F&& kernel_body,
                                         const On& placement = On(DeviceIndex{0}),
                                         NodeOptions options = {},
                                         const char* call = K_UNTRACKED_KERNEL)
            {
                std::vector<LivenessObserver> lives;
                const std::size_t device = placed_device(call, placement, lives);
                return record(Detail::untracked_launch_node(count, device,
                                                            std::forward<F>(kernel_body)),
                              lives, options);
            }

            /**
             * @brief Records a region-less launch that drives the handler itself.
             * @param kernel_body        Callable invoked as kernel_body(0, count, sycl::handler&).
             * @param placement Device the node runs on; a handle it names is watched.
             * @param call      Names the call the user wrote in a message.
             * @pre placement names a device of this runtime.
             */
            template <class F, std::enable_if_t<Detail::is_range_kernel_v<F>, int> = 0>
            std::size_t untracked_kernel(std::size_t count, F&& kernel_body,
                                         const On& placement = On(DeviceIndex{0}),
                                         NodeOptions options = {},
                                         const char* call = K_UNTRACKED_KERNEL)
            {
                std::vector<LivenessObserver> lives;
                const std::size_t device = placed_device(call, placement, lives);
                return record(Detail::untracked_range_node(count, device,
                                                           std::forward<F>(kernel_body)),
                              lives, options);
            }

            /**
             * @brief Records a launch over an Extent that names no region.
             * @param kernel_body        Callable invoked as kernel_body(sycl::id<N>) with the
             *                           origin applied.
             * @param placement Device the node runs on; a handle it names is watched.
             * @param call      Names the call the user wrote in a message.
             * @pre placement names a device of this runtime.
             */
            template <int N, class F>
            std::size_t untracked_kernel(Extent<N> extent, F&& kernel_body,
                                         const On& placement = On(DeviceIndex{0}),
                                         NodeOptions options = {},
                                         const char* call = K_UNTRACKED_KERNEL)
            {
                std::vector<LivenessObserver> lives;
                const std::size_t device = placed_device(call, placement, lives);
                return record(Detail::untracked_launch_node(extent, device,
                                                            std::forward<F>(kernel_body)),
                              lives, options);
            }

            /**
             * @brief Records a tiled launch over an NdRange that names no region.
             * @param kernel_body Callable invoked as kernel_body(sycl::nd_item<N>), or as
             *        kernel_body(sycl::nd_item<N>, sycl::id<N>) for the id with the origin.
             * @param placement Device the node runs on; a handle it names is watched.
             * @param call      Names the call the user wrote in a message.
             * @pre placement names a device of this runtime.
             */
            template <int N, class F>
            std::size_t untracked_kernel(NdRange<N> range, F&& kernel_body,
                                         const On& placement = On(DeviceIndex{0}),
                                         NodeOptions options = {},
                                         const char* call = K_UNTRACKED_KERNEL)
            {
                std::vector<LivenessObserver> lives;
                const std::size_t device = placed_device(call, placement, lives);
                return record(Detail::untracked_launch_node(range, device,
                                                            std::forward<F>(kernel_body)),
                              lives, options);
            }

            /**
             * @brief Records a raw command group that names no region.
             * @param kernel_body        Callable invoked as kernel_body(sycl::handler&).
             * @param placement Device the node runs on; a handle it names is watched.
             * @param call      Names the call the user wrote in a message.
             * @pre placement names a device of this runtime.
             */
            template <class F>
            std::size_t untracked_raw_kernel(
                       F&& kernel_body, const On& placement = On(DeviceIndex{0}),
                       NodeOptions options = {},
                       const char* call = "Recorder::untracked_raw_kernel(kernel, placement)")
            {
                std::vector<LivenessObserver> lives;
                const std::size_t device = placed_device(call, placement, lives);
                return record(
                    Detail::untracked_raw_node(device, std::forward<F>(kernel_body)),
                    lives, options);
            }

            /**
             * @brief Records the per-step rule of a State: read current, write next.
             * @param state The state the rule evolves; alive at every run, free to move.
             * @param kernel_body Callable invoked as kernel_body(i, const T* current, T* next)
             *        over the state's size.
             * @param call  Names the call the user wrote in a refusal's message.
             */
            template <typename T, class F>
            std::size_t state_rule(State<T>& state, F&& kernel_body, NodeOptions options = {},
                                   const char* call = K_STATE_RULE)
            {
                if (!admits(options) || !admit(empty_state_refusal(call, state)))
                    return rule_count();
                auto view = steps_.view_for(state);
                std::vector<LivenessObserver> lives;
                Detail::gather_life(lives, state);
                Detail::LiveFields live = live_fields_of(options, view);
                const std::size_t rule = record(
                    Detail::state_launch_node(view, view->size, std::forward<F>(kernel_body),
                                              std::move(live)),
                    lives, options);
                steps_.register_advance(std::move(view));
                return rule;
            }

            /**
             * @brief Records a State rule over a count, an Extent or an NdRange.
             * @param state The state the rule evolves; alive at every run, free to move.
             * @param kernel_body    Callable invoked as kernel_body(index, const T* current,
             *                       T* next).
             * @param call  Names the call the user wrote in a refusal's message.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            template <typename T, class Launch, class F,
                      std::enable_if_t<Detail::is_launch_v<Launch>, int> = 0>
            std::size_t state_rule(State<T>& state, Launch launch, F&& kernel_body,
                                   NodeOptions options = {},
                                   const char* call = K_LAUNCH_STATE_RULE)
            {
                if (!admits(options) || !admit(empty_state_refusal(call, state)) ||
                    !admit(Detail::launch_refusal(launch, state.size(), call, next_rule(),
                                                  "the State")))
                    return rule_count();
                auto view = steps_.view_for(state);
                std::vector<LivenessObserver> lives;
                Detail::gather_life(lives, state);
                Detail::LiveFields live = live_fields_of(options, view);
                const std::size_t rule = record(
                    Detail::state_launch_node(view, launch, std::forward<F>(kernel_body),
                                              std::move(live)),
                    lives, options);
                steps_.register_advance(std::move(view));
                return rule;
            }

            /**
             * @brief Records a State rule whose window is bound per step.
             * @param state The state the rule evolves; its size is the capacity. Alive at
             *        every run, free to move.
             * @param kernel_body Callable invoked as kernel_body(i, const T* current, T* next)
             *        for each live index.
             */
            template <typename T, class F>
            std::size_t dynamic_kernel(Dynamic window, State<T>& state, F&& kernel_body,
                                       NodeOptions options = {})
            {
                require_device_slots(window);
                if (!admits(options) || !admit(empty_state_refusal(K_DYNAMIC_STATE_RULE, state)))
                    return rule_count();
                auto view = steps_.view_for(state);

                std::vector<BufferRegion> slot_reads;
                std::vector<LivenessObserver> lives;
                auto bindings = Detail::make_late_bindings(window, slot_reads, lives);
                Result<std::uint32_t*> fault = fault_record_for(window);
                if (!admit(fault))
                    return rule_count();

                Detail::gather_life(lives, state);
                Detail::LiveFields live = live_fields_of(options, view);
                const std::size_t rule = record(
                    Detail::dynamic_state_node(
                        view, view->size, bindings, *fault, std::move(slot_reads),
                        std::forward<F>(kernel_body), std::move(live)),
                    lives, options);
                steps_.register_advance(std::move(view));
                steps_.register_binding_updater(std::move(bindings), std::move(window));
                return rule;
            }

            /**
             * @brief Records a whole-field rule that writes one State from several current fields.
             * @param states Coupled(out, in0, ...); each State is alive at every run and
             *        free to move.
             * @param kernel_body Callable invoked as kernel_body(i, current, next, in0, ...).
             * @param call Names the call the user wrote in a refusal's message.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            template <typename Out, typename... Inputs, class F>
            std::size_t coupled_rule(Coupled<Out, Inputs...> states, F&& kernel_body,
                                     NodeOptions options = {},
                                     const char* call = K_COUPLED_RULE)
            {
                return std::apply([&](State<Inputs>&... inputs)
                {
                    const bool output_holds = admit(empty_state_refusal(call, states.out));
                    const bool inputs_fit =
                        (admit(Detail::launch_refusal(states.out.size(), inputs.size(), call,
                                                      next_rule(), "a State read")) && ...);
                    if (!admits(options) || !output_holds || !inputs_fit)
                        return rule_count();
                    return record_coupled(states.out.size(), states.out,
                                          std::forward<F>(kernel_body), options, inputs...);
                }, states.inputs);
            }

            /**
             * @brief Records a coupled rule over a count, an Extent or an NdRange.
             * @param states Coupled(out, in0, ...); each State is alive at every run and
             *        free to move.
             * @param kernel_body Callable invoked as kernel_body(index, current, next, in0, ...).
             * @param call Names the call the user wrote in a refusal's message.
             */
            template <class Launch, typename Out, typename... Inputs, class F,
                      std::enable_if_t<Detail::is_launch_v<Launch>, int> = 0>
            std::size_t coupled_rule(Launch launch, Coupled<Out, Inputs...> states,
                                     F&& kernel_body, NodeOptions options = {},
                                     const char* call = K_LAUNCH_COUPLED_RULE)
            {
                return std::apply([&](State<Inputs>&... inputs)
                {
                    const bool output_fits =
                        admit(empty_state_refusal(call, states.out)) &&
                        admit(Detail::launch_refusal(launch, states.out.size(), call,
                                                     next_rule(), "the State written"));
                    const bool inputs_fit =
                        (admit(Detail::launch_refusal(launch, inputs.size(), call, next_rule(),
                                                      "a State read")) && ...);
                    if (!admits(options) || !output_fits || !inputs_fit)
                        return rule_count();
                    return record_coupled(launch, states.out, std::forward<F>(kernel_body),
                                          options, inputs...);
                }, states.inputs);
            }

            /**
             * @brief Records the single task that resolves K late counts for a run.
             * @param device Device whose context holds the fault record of a device count.
             * @param access What fill writes, and any further region it reads.
             * @param fill   Device callable run once as fill(live), in the order of counts.
             */
            template <std::size_t K, class Fill>
            std::size_t resolve(const std::array<LateCount, K>& counts, DeviceIndex device,
                                Access access, Fill fill, NodeOptions options = {})
            {
                std::vector<LivenessObserver> lives;
                bool reads_device = false;
                for (const LateCount& count : counts)
                    reads_device =
                        Detail::gather_device_read(count, access.reads, lives) || reads_device;
                Result<std::uint32_t*> fault =
                    reads_device ? fault_record(device) : host_fault_record();
                if (!admit(fault))
                    return rule_count();

                lives.insert(lives.end(), access.lives.begin(), access.lives.end());
                return record(Detail::late_count_resolver_node(counts, *fault, std::move(fill),
                                                               std::move(access.reads),
                                                               std::move(access.writes),
                                                               options.width),
                              lives, options);
            }

            /**
             * @brief Allocates USM this recorder owns and frees with itself.
             * @param count Elements to allocate; must be non-zero.
             * @return The base pointer, from which ordering regions are built, or an
             *         AllocationFailed error when the device has no room.
             * @pre The byte size of count fits std::size_t.
             */
            template <typename T>
            [[nodiscard]] Result<T*> scratch(std::size_t count, DeviceIndex device,
                                             Residency residency)
            {
                Result<void*> block = scratch_block(count, sizeof(T), device, residency);
                if (!block)
                    return make_error(std::move(block.error()));
                return static_cast<T*>(*block);
            }

            /**
             * @brief Names the rule most recently recorded; every node of it reports the name.
             * @param label Copied; an empty label returns the rule to the default name.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            void name_last_rule(std::string_view label)
            {
                if (rule_names_.empty())
                    return;
                rule_names_.back() = label_of(label);
                ++generation_;
            }

            /**
             * @brief Forgets every node, rule, observer, hook, scratch block and refusal.
             * @pre No step that runs the recorded nodes is pending.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            void clear()
            {
                // An open scope stays open, and its next node starts a new rule.
                scope_has_node_ = false;
                specs_.clear();
                rule_names_.clear();
                labels_.clear();
                scratch_.clear();
                observers_.clear();
                steps_ = Detail::StepMachinery();
                refusal_.reset();
                ++generation_;
            }

            /**
             * @brief Returns the first recording this recorder refused since clear().
             * @return The error the next run returns, or null when nothing was refused.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            [[nodiscard]] const Error* refusal() const noexcept
            {
                return refusal_ ? &*refusal_ : nullptr;
            }

            /** @brief Returns how many rules are recorded. */
            [[nodiscard]] std::size_t rule_count() const noexcept { return rule_names_.size(); }

            /** @brief Returns how many nodes are recorded. */
            [[nodiscard]] std::size_t node_count() const noexcept { return specs_.size(); }

            /**
             * @brief Returns a counter every change to the recorded work advances.
             * @return A value this recorder never returns again for different work.
             */
            [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }

            /** @brief Returns whether a recorded handle names a freed allocation. */
            [[nodiscard]] bool has_dangling_resource() const noexcept
            {
                for (const auto& observer : observers_)
                    if (observer.expired())
                        return true;
                return false;
            }

            /** @brief Returns how many distinct allocations has_dangling_resource() checks. */
            [[nodiscard]] std::size_t watched_allocation_count() const noexcept
            {
                return observers_.size();
            }

            /** @brief Returns the per-step hooks of the recorded nodes. */
            [[nodiscard]] const Detail::StepMachinery& steps() const noexcept { return steps_; }

        private:
            // A graph and a region own their recorder; they alone start one and move one.
            friend class Graph;
            friend class Region;
            // The plan builders of the library read the recorded nodes through this seam.
            friend struct Detail::RecorderReplay;

            /**
             * @brief Starts an empty recorder on a runtime.
             * @param context Runtime the recorder allocates scratch from and keeps alive.
             */
            explicit Recorder(ContextHandle context) noexcept
                : context_(std::move(context))
            {
            }

            /**
             * @brief Takes over another recorder's nodes, rules, scratch, hooks and refusal.
             * @param other Left empty on the same context; its generation() moves on.
             * @pre No rule scope is open on other.
             */
            Recorder(Recorder&& other) noexcept
            {
                take(other);
            }

            /**
             * @brief Replaces this recorder's contents with another's.
             * @param other Left empty on the same context; its generation() moves on.
             * @pre No rule scope is open on either recorder.
             */
            Recorder& operator=(Recorder&& other) noexcept
            {
                if (this != &other)
                    take(other);
                return *this;
            }

            /**
             * @brief Appends one node; every recording member ends here.
             * @param description What the library adds to a plan for the node; moved from.
             * @param lives   Observers of the handles the node names; moved from.
             * @param options What the caller stated; its name and parameters are copied.
             * @return The position of the rule the node belongs to, or rule_count() when
             *         options are refused.
             */
            SUSHIRUNTIME_API std::size_t record(Detail::NodeDescription&& description,
                                                std::vector<LivenessObserver>& lives,
                                                const NodeOptions& options = {});

            /**
             * @brief Allocates USM this recorder owns, for scratch().
             * @param count Elements to allocate; must be non-zero.
             * @return The base address, or an AllocationFailed error.
             * @pre The byte size of count elements fits std::size_t.
             */
            [[nodiscard]] SUSHIRUNTIME_API Result<void*> scratch_block(std::size_t count,
                                                                       std::size_t element_size,
                                                                       DeviceIndex device,
                                                                       Residency residency);

            /**
             * @brief Stops the process unless the device count slot and the base slot of a
             *        Dynamic are null or lie in host USM this runtime allocated.
             * @pre Each slot window names lies in a Residency::Host buffer of this runtime.
             */
            SUSHIRUNTIME_API void require_device_slots(const Dynamic& window) const;

            /** @brief Names state_rule(state, kernel) in a message of its own. */
            static constexpr const char* K_STATE_RULE = "Recorder::state_rule(state, kernel)";

            /** @brief Names state_rule(state, launch, kernel) in a message of its own. */
            static constexpr const char* K_LAUNCH_STATE_RULE =
                "Recorder::state_rule(state, launch, kernel)";

            /** @brief Names dynamic_kernel(window, state, kernel) in a message of its own. */
            static constexpr const char* K_DYNAMIC_STATE_RULE =
                "Recorder::dynamic_kernel(window, state, kernel)";

            /** @brief Names coupled_rule(Coupled, kernel) in a message of its own. */
            static constexpr const char* K_COUPLED_RULE =
                "Recorder::coupled_rule(Coupled(out, in0, ...), kernel)";

            /** @brief Names coupled_rule(launch, Coupled, kernel) in a message of its own. */
            static constexpr const char* K_LAUNCH_COUPLED_RULE =
                "Recorder::coupled_rule(launch, Coupled(out, in0, ...), kernel)";

            /** @brief Names the untracked_kernel() family in a message of its own. */
            static constexpr const char* K_UNTRACKED_KERNEL =
                "Recorder::untracked_kernel(launch, kernel, placement)";

            /**
             * @brief Records the node and the advance of a coupled rule already checked.
             * @tparam Launch std::size_t, Extent<N> or NdRange<N>.
             */
            template <class Launch, typename T, class F, class... Inputs>
            std::size_t record_coupled(Launch launch, State<T>& out, F&& kernel_body,
                                       const NodeOptions& options, State<Inputs>&... inputs)
            {
                std::vector<LivenessObserver> lives;
                Detail::gather_life(lives, out);
                (Detail::gather_life(lives, inputs), ...);

                auto out_view = steps_.view_for(out);
                auto views = std::make_tuple(steps_.view_for(inputs)...);
                Detail::LiveFields live = std::apply(
                    [&](const auto&... input_views)
                    {
                        return live_fields_of(options, out_view, input_views...);
                    },
                    views);
                const std::size_t rule = record(
                    Detail::coupled_launch_node(out_view, std::move(views), launch,
                                                std::forward<F>(kernel_body), std::move(live)),
                    lives, options);
                steps_.register_advance(std::move(out_view));
                return rule;
            }

            /**
             * @brief Returns the live fields of a State rule and registers their per-step
             *        refresh.
             * @param options What the rule states; a rule that names no operation gets null
             *        lists and registers nothing.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            template <typename T, class... Inputs>
            Detail::LiveFields live_fields_of(
                const NodeOptions& options, const std::shared_ptr<Detail::StateView<T>>& out,
                const std::shared_ptr<Detail::StateView<Inputs>>&... inputs)
            {
                Detail::LiveFields live;
                if (!options.operation.is_named())
                    return live;

                const auto refresh = [](const Detail::LiveFields& fields,
                                        const std::shared_ptr<Detail::StateView<T>>& written,
                                        const std::shared_ptr<Detail::StateView<Inputs>>&... read)
                {
                    *fields.reads = {static_cast<void*>(written->current),
                                     static_cast<void*>(read->current)...};
                    *fields.writes = {static_cast<void*>(written->next)};
                };
                live.reads = std::make_shared<std::vector<void*>>();
                live.writes = std::make_shared<std::vector<void*>>();
                refresh(live, out, inputs...);

                // The fields swap every step, so the lists are rewritten before each one.
                steps_.add_live_field_updater([refresh, live, out, inputs...]()
                {
                    refresh(live, out, inputs...);
                });
                return live;
            }

            /**
             * @brief Returns this recorder's own copy of a label.
             * @return A pointer valid until clear(), or null for an empty label.
             */
            const char* label_of(std::string_view label)
            {
                return label.empty() ? nullptr : labels_.emplace(label).first->c_str();
            }

            /**
             * @brief Returns the refusal of a rule over a State that owns no memory.
             * @param call Names the call the user wrote in the message.
             * @return An InvalidAccess error naming the rule, or nothing.
             */
            template <typename T>
            [[nodiscard]] std::optional<Error> empty_state_refusal(const char* call,
                                                                   const State<T>& state) const
            {
                if (!state.empty())
                    return std::nullopt;
                return Error{ErrorCode::InvalidAccess,
                             std::string(call) + ", rule " + std::to_string(next_rule()) +
                                 ": the State written owns no memory, so the rule has no "
                                 "element to evolve; pass a State that Runtime::state() "
                                 "returned with a size above zero."};
            }

            /** @brief Returns the position the next recorded node's rule has. */
            std::size_t next_rule() const noexcept
            {
                const bool joins = scope_open_ && scope_has_node_;
                return joins ? rule_names_.size() - 1 : rule_names_.size();
            }

            /**
             * @brief Hands over the observer of the handle a placement names.
             * @param call  Names the call the user wrote in the message.
             * @param lives Gains the placement's observer when it watches an allocation.
             * @return Index of the device the node is placed on.
             * @pre placement names a device of this runtime.
             */
            std::size_t placed_device(const char* call, const On& placement,
                                      std::vector<LivenessObserver>& lives) const
            {
                Detail::require_device(call, placement.device, Detail::device_count(context_));
                if (!placement.life.expired())
                    lives.push_back(placement.life);
                return placement.device.value();
            }

            /**
             * @brief Keeps a refusal when it is the first, and tells whether there was none.
             * @return True when refused is empty, so the recording may go on.
             */
            bool admit(std::optional<Error> refused)
            {
                if (!refused)
                    return true;
                if (!refusal_)
                    refusal_ = std::move(*refused);
                return false;
            }

            /**
             * @brief Keeps the error of a step of a recording that failed, as admit() does.
             * @return True when outcome holds a value, so the recording may go on.
             */
            template <typename Value>
            bool admit(Result<Value>& outcome)
            {
                if (outcome)
                    return true;
                admit(std::optional<Error>(std::move(outcome.error())));
                return false;
            }

            /** @brief Keeps the refusal of options a node cannot carry; true when they fit. */
            bool admits(const NodeOptions& options)
            {
                return admit(Detail::parameters_refusal(options, next_rule()));
            }

            /**
             * @brief Keeps one observer per allocation, however many nodes name it.
             * @param life Observer of a handle a node names.
             */
            void watch(LivenessObserver life)
            {
                // observers_ is sorted by owner; the binary search below depends on it.
                const auto at = std::lower_bound(observers_.begin(), observers_.end(), life,
                                                 std::owner_less<LivenessObserver>());
                if (at == observers_.end() || life.owner_before(*at))
                    observers_.insert(at, std::move(life));
            }

            /**
             * @brief Moves other's contents here and leaves other empty on its context.
             * @param other Recorder to empty; its generation advances by one. This one
             *        takes other's generation when it is the larger, or advances by one.
             */
            void take(Recorder& other) noexcept
            {
                context_ = other.context_;
                specs_ = std::exchange(other.specs_, {});
                rule_names_ = std::exchange(other.rule_names_, {});
                labels_ = std::exchange(other.labels_, {});
                scratch_ = std::exchange(other.scratch_, {});
                observers_ = std::exchange(other.observers_, {});
                steps_ = std::exchange(other.steps_, Detail::StepMachinery());
                refusal_ = std::exchange(other.refusal_, std::nullopt);
                // A value this recorder returned before must not name other's work.
                if (other.generation_ > generation_)
                    generation_ = other.generation_;
                else
                    ++generation_;
                ++other.generation_;
                scope_open_ = false;
                scope_has_node_ = false;
            }

            /**
             * @brief Allocates a zeroed fault record every finished step checks.
             * @param device Device whose context the record's host USM belongs to.
             * @return K_DEVICE_FAULT_WORDS words this recorder owns, or the error of scratch().
             */
            SUSHIRUNTIME_API Result<std::uint32_t*> fault_record(DeviceIndex device);

            /**
             * @brief Allocates a zeroed fault record only host code writes.
             * @return K_DEVICE_FAULT_WORDS words of host memory this recorder owns, or an
             *         AllocationFailed error.
             */
            SUSHIRUNTIME_API Result<std::uint32_t*> host_fault_record();

            /**
             * @brief Allocates a fault record for a late-bound node.
             * @param window A Dynamic that passed require_device_slots().
             * @return The record, in host USM of the slot's device or in host memory when
             *         window has no device slot, or the error of the allocation.
             */
            SUSHIRUNTIME_API Result<std::uint32_t*> fault_record_for(const Dynamic& window);

            /** @brief The runtime scratch comes from; the scratch keeps it alive. */
            ContextHandle context_;

            /** @brief The recorded nodes, in recording order. */
            std::vector<Detail::NodeSpec> specs_;

            /** @brief Owns the USM handed out by scratch() and the fault records. */
            std::vector<std::shared_ptr<void>> scratch_;

            /** @brief One entry per rule: its label in labels_, or null for the default. */
            std::vector<const char*> rule_names_;

            /** @brief Owns one copy of each distinct label; its entries never move. */
            std::unordered_set<std::string> labels_;

            /** @brief One liveness observer per allocation the nodes name, sorted by owner. */
            std::vector<LivenessObserver> observers_;

            /** @brief The per-step hooks of the recorded nodes. */
            Detail::StepMachinery steps_;

            /** @brief The first refused recording since clear(); empty when none. */
            std::optional<Error> refusal_;

            /** @brief Advanced by every recorded node, rename, clear() and move. */
            std::uint64_t generation_ = 0;

            /** @brief Whether a rule scope is open at this moment. */
            bool scope_open_ = false;

            /** @brief Whether the open rule scope has recorded a node. */
            bool scope_has_node_ = false;
    };
} // namespace SushiRuntime
