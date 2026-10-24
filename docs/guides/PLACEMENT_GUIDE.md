# Placement guide

How a program says where its work runs without writing it into the code that records the work:
the placement class and key a node carries, the scopes that give them, and the placement rules a
person writes for one machine or for a cluster.

The runtime computes nothing of its own here. A library such as SushiBLAS records its operations
as it always does; the program that owns the graph says which group the work belongs to; the
person running it says where each group goes.

## 1. Class and key

A node carries two facts in `NodeOptions`: a `PlacementClass`, built from a dotted name such as
`sushiai.replica` or `sushiengine.physics`, and an optional placement key, such as a replica or an
island index. `0` is a key like any other; no key is `std::nullopt`. A node that states neither
runs where it ran before rules existed.

A class keeps the hash of its whole name and of each prefix that ends before a dot, up to four of
them, so a rule can name a family of classes by its prefix. It stores no name and owns nothing.

## 2. Scopes

A scope gives every node recorded inside it a class and a key, so the code that records the work
does not have to know about placement at all:

```cpp
        const PlacementScope outer = rt.placement_scope("outer.group", 7u);
        const PlacementScope inner = rt.placement_scope("inner.group", 0u);
        // ...
        recorder.within(outer, [&](Recorder& in_outer)
        {
            record_idle(in_outer);
            in_outer.within(inner, [&](Recorder& in_inner)
            {
                record_idle(in_inner);
                record_idle(in_inner, own_class_and_key);
            });
            record_idle(in_outer);
            record_idle(in_outer, own_class_without_key);
        });
```

Source: `tests/surface/api/graph/test_recorder.cpp`

Scopes nest and the inner one wins. A node whose own options name a class keeps its own class and
its own key, even when its own key is none. The scope is a value passed to `within()`, never
ambient state. A malformed name, such as an empty segment, stops the process.

A scope also places memory. `PlacementScope::buffer<T>()` allocates on the device the rules give
the scope, and `On(scope)` gives an untracked kernel that device.

## 3. Rules

A rule is plain data that a program fills from its own file format; the runtime reads no rule
file. It has a match, a target and a policy:

| Part | What it states |
| --- | --- |
| Match | a class (`a.b`), a class prefix (`a.b.*`, which matches the classes below `a.b` and not `a.b` itself) or any class; a key; an operation name |
| Target | a machine by name or by a `key=value` label; a device by index, by kind or by a least amount of memory |
| Policy | a `priority` (larger wins; equals resolve by the order they were added), what happens when the target is absent (`Local`, `Wait` with a bound, `Fail`), a `deadline`, a `name` for messages |

`PlacementRules::add()` checks each rule and refuses a malformed one with
`ErrorCode::InvalidArgument` naming the rule: one with no match, an empty segment, a `*` other
than a trailing `.*`, a target with no machine and no device, two machine or two device fields, or
a bound or deadline that is not positive.

```cpp
        PlacementRule rule = pinned_rule();
        rule.target.machine_name = "rack-7-gpu";
        rule.if_absent.action = AbsentAction::Wait;
        rule.if_absent.bound = std::chrono::duration_cast<std::chrono::milliseconds>(K_WAIT);
        set_rule(master_runtime, std::move(rule));
```

Source: `tests/extension/distributed/test_distributed.cpp`

`Runtime::set_placement_rules()` takes a rule set between two runs and is refused with
`ErrorCode::InvalidState` while a run is in flight; `clear_placement_rules()` removes it. The next
compile of each graph resolves every node's class, key and operation to its winning rule once, so
a replay reads no rule; `PlanNode::placement_rule` reports the winner.

## 4. On one machine

The rules decide where data lives, and a tracked node runs where its memory lives. A scope's
buffer lands on the device its rule names: the device of that index, the first of that kind, or
the first with at least that much memory. A rule whose machine part names another machine makes
the target absent here. When the target is absent, `Local` gives device 0, `Fail` returns
`ErrorCode::InvalidArgument` naming the rule and the device, and `Wait` gives device 0 after its
bound, since a local device never appears during a run.

A node may state its own device; it wins over the device that owns its memory when the two share a
SYCL context, and the node is refused when they do not. The runtime never copies data between
devices on its own: a copy is a node the program records.

## 5. Across machines

With a cluster attached, a node whose winning rule names a worker, by its name or by one of its
labels, is dispatched to that worker, on the device the rule's device part picks from the facts
the worker reported. A worker reports its name, its operating system, its devices and its labels
when it connects, and keeps them across a reconnect under the same name; a person adds labels with
`--label key=value` or `SR_WORKER_LABELS`.

| The target worker is | What happens |
| --- | --- |
| Alive, with a free slot | The node is dispatched to it, on the chosen device |
| Alive, at its job limit | The node is held and offered again when the worker frees a slot, whatever the rule's absent policy says |
| Absent | `Local` runs the node on the master; `Wait` holds it until the worker returns or the bound passes, then runs it on the master; `Fail` ends the run with an error naming the rule and the target |
| This machine | The node runs on the master |

A rule's `deadline` replaces the cluster's `task_deadline` for the nodes it places. With no rule
set, or for a node no rule matches, the offload policy decides as it always did.

[The distributed guide](DISTRIBUTED_GUIDE.md) says how to start a master and its workers;
`modules/foundation/placement/README.md` holds the rule model's details.
