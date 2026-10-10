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
#include <SushiRuntime/api/detail/callable_traits.hpp>
#include <SushiRuntime/api/detail/device_fault.hpp>
#include <SushiRuntime/api/detail/emit_captured.hpp>
#include <SushiRuntime/api/detail/emit_device_sized.hpp>
#include <SushiRuntime/api/detail/emit_dynamic.hpp>
#include <SushiRuntime/api/detail/emit_host.hpp>
#include <SushiRuntime/api/detail/emit_late_count_resolver.hpp>
#include <SushiRuntime/api/detail/emit_offload.hpp>
#include <SushiRuntime/api/detail/emit_state.hpp>
#include <SushiRuntime/api/detail/emit_tracked.hpp>
#include <SushiRuntime/api/detail/emit_untracked.hpp>
#include <SushiRuntime/api/detail/handle_access.hpp>
#include <SushiRuntime/api/detail/host_slot_check.hpp>
#include <SushiRuntime/api/detail/late_count_resolution.hpp>
#include <SushiRuntime/api/detail/node_spec.hpp>
#include <SushiRuntime/api/detail/step_machinery.hpp>
#include <SushiRuntime/api/detail/usm_scratch.hpp>
#include <SushiRuntime/api/graph/access.hpp>
#include <SushiRuntime/api/graph/backing_graph.hpp>
#include <SushiRuntime/api/graph/device_sized.hpp>
#include <SushiRuntime/api/graph/node_options.hpp>
#include <SushiRuntime/api/launch/extent.hpp>
#include <SushiRuntime/api/launch/nd_range.hpp>
#include <SushiRuntime/api/vocabulary/dependencies.hpp>
#include <SushiRuntime/api/vocabulary/dynamic.hpp>
#include <SushiRuntime/api/vocabulary/late_count.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/memory/resource_liveness.hpp>
#include <SushiRuntime/memory/resource_region.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/observability/dispatch_width.hpp>
#include <SushiRuntime/task/late_bindings.hpp>
#include <SushiRuntime/task/task_types.hpp>

namespace SushiRuntime
{
    namespace API
    {
        /**
         * @brief Owns the nodes recorded for one graph or region and replays them.
         *
         * A move leaves the source empty on the same context, ready to record again.
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

                /**
                 * @brief Starts an empty recorder on a runtime context.
                 * @param context Context the recorder allocates scratch from and keeps alive.
                 */
                explicit Recorder(std::shared_ptr<Execution::RuntimeContext> context)
                    : context_(std::move(context))
                {
                }

                /**
                 * @brief Takes over another recorder's nodes, rules, scratch and hooks.
                 * @param other Left empty on the same context, with its counters at zero.
                 * @pre No rule scope is open on other.
                 */
                Recorder(Recorder&& other) noexcept
                {
                    take(other);
                }

                /**
                 * @brief Replaces this recorder's contents with another's.
                 * @param other Left empty on the same context, with its counters at zero.
                 * @pre No rule scope is open on either recorder.
                 */
                Recorder& operator=(Recorder&& other) noexcept
                {
                    if (this != &other)
                        take(other);
                    return *this;
                }

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
                 * @brief Records a per-element launch over [0, count), ordered by its access.
                 * @param fn Callable invoked as fn(i) for each index.
                 */
                template <class F, std::enable_if_t<is_per_element_v<F>, int> = 0>
                void kernel(std::size_t count, Access access, F&& fn, NodeOptions options = {})
                {
                    record(detail::per_element_node(std::move(access.reads),
                                                    std::move(access.writes),
                                                    count, std::forward<F>(fn), options.width),
                           access.lives);
                }

                /**
                 * @brief Records a launch that drives the handler over [0, count) itself.
                 * @param fn Callable invoked as fn(0, count, sycl::handler&).
                 */
                template <class F, std::enable_if_t<is_range_kernel_v<F>, int> = 0>
                void kernel(std::size_t count, Access access, F&& fn, NodeOptions options = {})
                {
                    record(detail::range_node(std::move(access.reads), std::move(access.writes),
                                              count, std::forward<F>(fn), options.width),
                           access.lives);
                }

                /**
                 * @brief Records a launch over an Extent, ordered by its access.
                 * @param fn Callable invoked as fn(sycl::id<N>) with the extent's offset applied.
                 */
                template <int N, class F>
                void kernel(Extent<N> extent, Access access, F&& fn, NodeOptions options = {})
                {
                    record(detail::extent_node(std::move(access.reads), std::move(access.writes),
                                               extent, std::forward<F>(fn), options.width),
                           access.lives);
                }

                /**
                 * @brief Records a tiled launch over an NdRange, ordered by its access.
                 * @param fn Callable invoked as fn(sycl::nd_item<N>).
                 */
                template <int N, class F>
                void kernel(NdRange<N> range, Access access, F&& fn, NodeOptions options = {})
                {
                    record(detail::nd_range_node(std::move(access.reads), std::move(access.writes),
                                                 range, std::forward<F>(fn), options.width),
                           access.lives);
                }

                /**
                 * @brief Records a launch run at capacity that reads its live width on the device.
                 * @param fn Callable invoked as fn(t), fn(t, w0) or fn(t, w0, w1); the words come
                 *        from launch.live_words.
                 * @pre access names the regions launch.live_width and launch.live_words lie in.
                 */
                template <class F>
                void kernel(DeviceSized launch, Access access, F&& fn, NodeOptions options = {})
                {
                    record(detail::device_sized_node(std::move(access.reads),
                                                     std::move(access.writes), launch,
                                                     std::forward<F>(fn), options.width),
                           access.lives);
                }

                /**
                 * @brief Records a per-element launch whose window is bound per step.
                 * @param capacity Upper bound the live window must fit; also the default width.
                 * @throws Core::SushiException With invalid_access for a slot outside host USM.
                 */
                template <class F, std::enable_if_t<is_per_element_v<F>, int> = 0>
                void kernel(Dynamic window, std::size_t capacity, Access access, F&& fn,
                            NodeOptions options = {})
                {
                    detail::require_host_device_slots(*context_, window);
                    auto bindings = std::make_shared<::SushiRuntime::Graph::LateBindings>();
                    detail::adopt_device_bindings(*bindings, window, access.reads);
                    std::uint32_t* fault = fault_record_for(window);
                    const std::size_t width =
                        options.width == Core::WIDTH_UNKNOWN ? capacity : options.width;

                    record(detail::dynamic_node(bindings, fault, std::move(access.reads),
                                                std::move(access.writes), capacity,
                                                std::forward<F>(fn), width),
                           access.lives);
                    steps_.register_binding_updater(std::move(bindings), std::move(window));
                }

                /**
                 * @brief Records a raw command group, ordered by its access.
                 * @param fn Callable invoked as fn(sycl::handler&).
                 */
                template <class F>
                void raw_kernel(Access access, F&& fn, NodeOptions options = {})
                {
                    record(detail::raw_node(std::move(access.reads), std::move(access.writes),
                                            std::forward<F>(fn), options.width),
                           access.lives);
                }

                /**
                 * @brief Records an Extent launch whose kernel gets one raw pointer per capture.
                 * @param pointers A std::tuple of pointers, passed to fn after the shifted id.
                 */
                template <int N, class Pointers, class F>
                void captured_kernel(Extent<N> extent, Access access, Pointers pointers, F&& fn,
                                     NodeOptions options = {})
                {
                    record(detail::captured_node(std::move(access.reads), std::move(access.writes),
                                                 extent, std::move(pointers), std::forward<F>(fn),
                                                 options.width),
                           access.lives);
                }

                /**
                 * @brief Records a task that runs once on a worker thread, ordered by its access.
                 * @param fn Nullary callable; must not submit SYCL work.
                 */
                template <class F>
                void host(Access access, F&& fn, NodeOptions options = {})
                {
                    record(detail::host_node(std::move(access.reads), std::move(access.writes),
                                             std::forward<F>(fn), options.width),
                           access.lives);
                }

                /**
                 * @brief Records an emitter that adds its own nodes at every replay.
                 * @param fn Callable invoked as fn(BackingGraph&); it only adds nodes.
                 */
                template <class F>
                void emitter(F&& fn)
                {
                    std::vector<Core::LivenessObserver> lives;
                    record([emit = std::forward<F>(fn)]
                           (BackingGraph& bg, std::optional<std::uint32_t>)
                           {
                               emit(bg);
                           }, lives);
                    // An emitter stamps its own nodes, so it takes no registration from a rule.
                    specs_.back().takes_rule_registration = false;
                }

                /**
                 * @brief Records a per-element launch over [0, count) that names no region.
                 * @param fn Callable invoked as fn(i) for each index.
                 */
                template <class F, std::enable_if_t<is_per_element_v<F>, int> = 0>
                void untracked_kernel(std::size_t count, F&& fn)
                {
                    std::vector<Core::LivenessObserver> lives;
                    record(detail::untracked_per_element_node(count, std::forward<F>(fn)), lives);
                }

                /**
                 * @brief Records a region-less launch that drives the handler itself.
                 * @param fn Callable invoked as fn(0, count, sycl::handler&).
                 */
                template <class F, std::enable_if_t<is_range_kernel_v<F>, int> = 0>
                void untracked_kernel(std::size_t count, F&& fn)
                {
                    std::vector<Core::LivenessObserver> lives;
                    record(detail::untracked_range_node(count, std::forward<F>(fn)), lives);
                }

                /**
                 * @brief Records a launch over an Extent that names no region.
                 * @param fn Callable invoked as fn(sycl::id<N>) with the extent's offset applied.
                 */
                template <int N, class F>
                void untracked_kernel(Extent<N> extent, F&& fn)
                {
                    std::vector<Core::LivenessObserver> lives;
                    record(detail::untracked_extent_node(extent, std::forward<F>(fn)), lives);
                }

                /**
                 * @brief Records a tiled launch over an NdRange that names no region.
                 * @param fn Callable invoked as fn(sycl::nd_item<N>).
                 */
                template <int N, class F>
                void untracked_kernel(NdRange<N> range, F&& fn)
                {
                    std::vector<Core::LivenessObserver> lives;
                    record(detail::untracked_nd_range_node(range, std::forward<F>(fn)), lives);
                }

                /**
                 * @brief Records a raw command group that names no region.
                 * @param fn Callable invoked as fn(sycl::handler&).
                 */
                template <class F>
                void untracked_raw_kernel(F&& fn)
                {
                    std::vector<Core::LivenessObserver> lives;
                    record(detail::untracked_raw_node(std::forward<F>(fn)), lives);
                }

                /**
                 * @brief Records the per-step rule of a State: read current, write next.
                 * @param state The state the rule evolves; must outlive this recorder.
                 * @param fn Callable invoked as fn(i, const T* cur, T* next) over the state's size.
                 */
                template <typename T, class F>
                void state_rule(State<T>& state, F&& fn)
                {
                    auto view = steps_.view_for(state);
                    std::vector<Core::LivenessObserver> lives;
                    detail::gather_life(lives, state);
                    record(detail::state_node(view, &state, std::forward<F>(fn)), lives);
                    steps_.register_advance(state, view);
                }

                /**
                 * @brief Records a State rule launched over an Extent.
                 * @param state The state the rule evolves; must outlive this recorder.
                 * @param fn Callable invoked as fn(sycl::id<N>, const T* cur, T* next).
                 */
                template <typename T, int N, class F>
                void state_rule(State<T>& state, Extent<N> extent, F&& fn)
                {
                    auto view = steps_.view_for(state);
                    std::vector<Core::LivenessObserver> lives;
                    detail::gather_life(lives, state);
                    record(detail::state_extent_node(view, &state, extent, std::forward<F>(fn)),
                           lives);
                    steps_.register_advance(state, view);
                }

                /**
                 * @brief Records a State rule whose window is bound per step.
                 * @param state The state the rule evolves; its size is the capacity.
                 * @throws Core::SushiException With invalid_access for a slot outside host USM.
                 */
                template <typename T, class F>
                void state_rule(Dynamic window, State<T>& state, F&& fn)
                {
                    detail::require_host_device_slots(*context_, window);
                    auto view = steps_.view_for(state);
                    auto bindings = std::make_shared<::SushiRuntime::Graph::LateBindings>();

                    std::vector<Core::ResourceRegion> reads{ detail::HandleAccess::read_key(state) };
                    detail::adopt_device_bindings(*bindings, window, reads);
                    std::uint32_t* fault = fault_record_for(window);

                    std::vector<Core::LivenessObserver> lives;
                    detail::gather_life(lives, state);
                    record(detail::dynamic_state_node(view, &state, bindings, fault,
                                                      std::move(reads),
                                                      std::forward<F>(fn)),
                           lives);
                    steps_.register_advance(state, view);
                    steps_.register_binding_updater(std::move(bindings), std::move(window));
                }

                /**
                 * @brief Records a rule that writes one State from several current fields.
                 * @param out    The field written; must outlive this recorder.
                 * @param fn     Callable invoked as fn(i, cur, next, a, ...).
                 * @param inputs The fields read; each must outlive this recorder.
                 */
                template <typename T, class F, class... Inputs>
                void coupled_rule(State<T>& out, F&& fn, State<Inputs>&... inputs)
                {
                    std::vector<Core::LivenessObserver> lives;
                    detail::gather_life(lives, out);
                    (detail::gather_life(lives, inputs), ...);

                    auto out_view = steps_.view_for(out);
                    auto views = std::make_tuple(steps_.view_for(inputs)...);
                    auto states = std::make_tuple(&out, &inputs...);
                    record(detail::coupled_node(out_view, &out, views, states, std::forward<F>(fn)),
                           lives);
                    steps_.register_advance(out, out_view);
                }

                /**
                 * @brief Records an offloadable per-element task with a local fallback.
                 * @param local  Callable invoked as local(i) for i in [0, count) when run here.
                 * @param params Scalar parameters sent with the kernel name.
                 */
                template <class F>
                void offload(::SushiRuntime::Graph::OpID op, std::size_t count, Access access,
                             F&& local, std::vector<std::uint64_t> params)
                {
                    record(detail::offload_node(op, count, std::move(access.reads),
                                                std::move(access.writes), std::forward<F>(local),
                                                std::move(params)),
                           access.lives);
                }

                /**
                 * @brief Records an offloadable per-step rule of a State.
                 * @pre state was registered with State::distributed().
                 * @param local Callable invoked as local(i, const T* cur, T* next) when run here.
                 */
                template <typename T, class F>
                void offload_rule(::SushiRuntime::Graph::OpID op, State<T>& state, F&& local,
                                  std::vector<std::uint64_t> params)
                {
                    auto view = steps_.view_for(state);
                    auto live_reads = std::make_shared<std::vector<void*>>(
                        std::vector<void*>{ detail::HandleAccess::read_key(state) });
                    auto live_writes = std::make_shared<std::vector<void*>>(
                        std::vector<void*>{ detail::HandleAccess::write_key(state) });

                    std::vector<Core::LivenessObserver> lives;
                    detail::gather_life(lives, state);
                    record(detail::offload_state_node(op, view, &state, live_reads, live_writes,
                                                      std::forward<F>(local), std::move(params)),
                           lives);
                    steps_.register_advance(state, view);

                    // Live IO is re-pointed before each step for the coordinator.
                    steps_.add_dist_io_updater([view, live_reads, live_writes]()
                    {
                        (*live_reads)[0]  = view->cur;
                        (*live_writes)[0] = view->next;
                    });
                }

                /**
                 * @brief Records the single task that resolves K late counts for a run.
                 * @param device Device whose context holds the fault record of a device count.
                 * @param access What fill writes, and any further region it reads.
                 * @param fill   Device callable run once as fill(live), in the order of counts.
                 * @throws Core::SushiException With invalid_access for a device slot outside
                 *         host USM, or invalid_graph for a capacity past a uint32 slot.
                 */
                template <std::size_t K, class Fill>
                void resolve(const std::array<LateCount, K>& counts, DeviceIndex device,
                             Access access, Fill fill, NodeOptions options = {})
                {
                    for (const LateCount& count : counts)
                        detail::validate_late_count(count, "Recorder::resolve");

                    std::vector<Core::LivenessObserver> lives;
                    bool reads_device = false;
                    for (const LateCount& count : counts)
                    {
                        if (count.host || count.device == nullptr)
                            continue;
                        const Reads slot(*count.device);
                        access.reads.insert(access.reads.end(), slot.keys.begin(), slot.keys.end());
                        lives.insert(lives.end(), slot.lives.begin(), slot.lives.end());
                        reads_device = true;
                    }
                    std::uint32_t* fault = reads_device ? fault_record(device) : nullptr;

                    lives.insert(lives.end(), access.lives.begin(), access.lives.end());
                    record(detail::late_count_resolver_node(counts, fault, std::move(fill),
                                                            std::move(access.reads),
                                                            std::move(access.writes),
                                                            options.width),
                           lives);
                }

                /**
                 * @brief Allocates USM this recorder owns and frees with itself.
                 * @param count Elements to allocate; must be non-zero.
                 * @return The base pointer; build ordering regions from it.
                 * @throws Core::SushiException With allocation_failed when the device has no room.
                 */
                template <typename T>
                [[nodiscard]] T* scratch(std::size_t count, DeviceIndex device, Residency residency)
                {
                    std::shared_ptr<T> owned =
                        detail::make_usm_scratch<T>(context_, count, device, residency);
                    scratch_.push_back(std::static_pointer_cast<void>(owned));
                    return owned.get();
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
                    rule_names_.back() =
                        label.empty() ? nullptr : labels_.emplace(label).first->c_str();
                    ++generation_;
                }

                /**
                 * @brief Forgets every node, rule, observer, hook and scratch block recorded.
                 * @pre No rule scope is open, and no step that runs the recorded nodes is pending.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                void clear()
                {
                    specs_.clear();
                    rule_names_.clear();
                    labels_.clear();
                    scratch_.clear();
                    observers_.clear();
                    steps_ = detail::StepMachinery();
                    ++generation_;
                }

                /** @brief Returns how many rules are recorded. */
                [[nodiscard]] std::size_t rule_count() const noexcept { return rule_names_.size(); }

                /** @brief Returns how many nodes are recorded. */
                [[nodiscard]] std::size_t node_count() const noexcept { return specs_.size(); }

                /** @brief Returns a counter every change to the recorded work advances. */
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

                /**
                 * @brief Adds every recorded node to a backing graph, in recording order.
                 *
                 * The nodes of one rule share a registration index reserved on target when the
                 * first of them is added, and all carry the rule's name.
                 * @param visit Called as visit(rule, node) once node is added and named.
                 */
                template <class Visit>
                void replay(BackingGraph& target, Visit&& visit) const
                {
                    std::uint32_t stamped = detail::NodeSpec::K_NO_RULE;
                    std::optional<std::uint32_t> reserved;
                    for (std::size_t node = 0; node < specs_.size(); ++node)
                    {
                        const detail::NodeSpec& spec = specs_[node];
                        if (spec.takes_rule_registration && spec.rule != stamped)
                        {
                            stamped = spec.rule;
                            reserved = target.reserve_registration();
                        }
                        spec.emit(target, spec.takes_rule_registration
                                              ? reserved : std::optional<std::uint32_t>());
                        target.name_last_node(rule_names_[spec.rule]);
                        visit(static_cast<std::size_t>(spec.rule), node);
                    }
                }

                /** @brief Adds every recorded node to a backing graph, in recording order. */
                void replay(BackingGraph& target) const
                {
                    replay(target, [](std::size_t, std::size_t) {});
                }

                /** @brief Returns the per-step hooks of the recorded nodes. */
                [[nodiscard]] const detail::StepMachinery& steps() const noexcept { return steps_; }

            private:
                /**
                 * @brief Appends one node; every recording member ends here.
                 * @param emit  Replay callable invoked as emit(BackingGraph&, registration).
                 * @param lives Observers of the handles the node names; moved from.
                 */
                template <class Emit>
                void record(Emit&& emit, std::vector<Core::LivenessObserver>& lives)
                {
                    detail::NodeSpec spec(std::forward<Emit>(emit));
                    if (!scope_open_ || !scope_has_node_)
                        rule_names_.push_back(nullptr);
                    scope_has_node_ = true;
                    spec.rule = static_cast<std::uint32_t>(rule_names_.size() - 1);

                    specs_.push_back(std::move(spec));
                    for (Core::LivenessObserver& life : lives)
                        watch(std::move(life));
                    ++generation_;
                }

                /**
                 * @brief Keeps one observer per allocation, however many nodes name it.
                 * @param life Observer of a handle a node names.
                 */
                void watch(Core::LivenessObserver life)
                {
                    // observers_ is sorted by owner; the binary search below depends on it.
                    const auto at = std::lower_bound(observers_.begin(), observers_.end(), life,
                                                     std::owner_less<Core::LivenessObserver>());
                    if (at == observers_.end() || life.owner_before(*at))
                        observers_.insert(at, std::move(life));
                }

                /**
                 * @brief Moves other's contents here and leaves other empty on its context.
                 * @param other Recorder to empty; its counters return to zero.
                 */
                void take(Recorder& other) noexcept
                {
                    context_ = other.context_;
                    specs_ = std::exchange(other.specs_, {});
                    rule_names_ = std::exchange(other.rule_names_, {});
                    labels_ = std::exchange(other.labels_, {});
                    scratch_ = std::exchange(other.scratch_, {});
                    observers_ = std::exchange(other.observers_, {});
                    steps_ = std::exchange(other.steps_, detail::StepMachinery());
                    generation_ = std::exchange(other.generation_, 0);
                    scope_open_ = false;
                    scope_has_node_ = false;
                }

                /**
                 * @brief Allocates a zeroed fault record every finished step checks.
                 * @param device Device whose context the record's host USM belongs to.
                 * @return DEVICE_FAULT_WORDS words this recorder owns.
                 */
                std::uint32_t* fault_record(DeviceIndex device)
                {
                    std::uint32_t* words =
                        scratch<std::uint32_t>(detail::DEVICE_FAULT_WORDS, device, Residency::Host);
                    for (std::size_t w = 0; w < detail::DEVICE_FAULT_WORDS; ++w)
                        words[w] = 0;
                    steps_.register_fault_record(words);
                    return words;
                }

                /**
                 * @brief Allocates a fault record for a Dynamic with a device slot.
                 * @param window A Dynamic that passed require_host_device_slots().
                 * @return The record, or null when window has no device slot.
                 */
                std::uint32_t* fault_record_for(const Dynamic& window)
                {
                    const std::uint32_t* slot = (window.device_count != nullptr)
                        ? window.device_count : window.device_base;
                    if (slot == nullptr)
                        return nullptr;
                    void* address = const_cast<void*>(static_cast<const void*>(slot));
                    Memory::UsmAllocator& memory = context_->memory();
                    const std::optional<void*> base = memory.containing_base(address);
                    const std::optional<std::size_t> device =
                        memory.device_of(base ? *base : address);
                    return fault_record(DeviceIndex{device.value_or(0)});
                }

                /** @brief The context scratch comes from; the scratch keeps it alive. */
                std::shared_ptr<Execution::RuntimeContext> context_;

                /** @brief The recorded nodes, in recording order. */
                std::vector<detail::NodeSpec> specs_;

                /** @brief Owns the USM handed out by scratch() and the fault records. */
                std::vector<std::shared_ptr<void>> scratch_;

                /** @brief One entry per rule: its label in labels_, or null for the default. */
                std::vector<const char*> rule_names_;

                /** @brief Owns one copy of each distinct label; its entries never move. */
                std::unordered_set<std::string> labels_;

                /** @brief One liveness observer per allocation the nodes name, sorted by owner. */
                std::vector<Core::LivenessObserver> observers_;

                /** @brief The per-step hooks of the recorded nodes. */
                detail::StepMachinery steps_;

                /** @brief Advanced by every recorded node, every rename and every clear(). */
                std::uint64_t generation_ = 0;

                /** @brief Whether a rule scope is open at this moment. */
                bool scope_open_ = false;

                /** @brief Whether the open rule scope has recorded a node. */
                bool scope_has_node_ = false;
        };

    } // namespace API
} // namespace SushiRuntime
