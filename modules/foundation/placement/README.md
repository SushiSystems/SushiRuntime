# placement

The rule model that says where a node runs: the plain records a caller fills from its own format,
and the rule set that checks them, keeps them in order and picks the rule that wins for a node.

It depends on `core`, for `Result`, `PlacementClass`, `OperationID` and the export macro, and on
`machine`, for `NodeLabel` and `DeviceKind`. It includes no SYCL header, so a module that only moves
rules between machines does not compile against SYCL.

## Public entry points

| Header | Holds |
| --- | --- |
| `placement_rule.hpp` | `PlacementRule` with `PlacementMatch`, `PlacementTarget`, `AbsentPolicy` and `AbsentAction`, plain records with no behaviour |
| `placement_rules.hpp` | `PlacementRules`: `add()` checks and keeps a rule, `resolve()` returns the position of the winning rule, `rule()`, `size()` and `empty()` read the set |

Every name is declared in `SushiRuntime`. `PlacementRules` is exported with `SUSHIRUNTIME_API` and
built from `source/placement_rules.cpp`.

## Matching and winning

A rule matches a node when every part the rule states matches; a part left empty matches anything.

| Part of the rule | Matches a node when |
| --- | --- |
| `match.placement_class` empty | always, including a node with no class |
| `match.placement_class` of the form `a.b` | the node's class holds the hash of `a.b` |
| `match.placement_class` of the form `a.b.*` | the hash of `a.b` is among the prefix hashes of the node's class, so `a.b` itself does not match |
| `match.key` set | the node has a key and the two are equal; 0 is a key like any other |
| `match.operation` set | the node's operation id equals the id of that name |

When several rules match, the one with the largest `priority` wins; among equal priorities, the one
added first. No rule matching gives no position, and the caller does what it does without rules.

## Checks of add()

`add()` returns `ErrorCode::InvalidArgument` and keeps nothing when a rule breaks one of these. The
message names the rule by its `name`, or by its position when the name is empty, and says what to
pass instead.

| Part | Refused when |
| --- | --- |
| Match | class, key and operation are all empty; a class has an empty segment; a `*` stands anywhere but a trailing `.*`; a prefix has more than `PlacementClass::K_MAX_PREFIX_DEPTH` segments |
| Target | no machine and no device; both `machine_name` and `machine_label`; more than one of `device_index`, `device_kind` and `device_min_memory`; a label key that is empty or holds `=` |
| Policy | a `Wait` bound or a `deadline` that is not positive |

An exact class of any depth is accepted, since `PlacementClass` compares it by its whole-name hash.

## placement_rules.hpp

### The precomputed form

`add()` stores each rule in a private `Entry` beside what `resolve()` compares: how the class is
matched, the hash of the class or prefix, and the `OperationID` of the operation. `resolve()` reads
only those hashes and the priority, so it never touches a string. The design is in
[`docs/design/LAYERED_EXECUTION/README.md`](../../../docs/design/LAYERED_EXECUTION/README.md),
section "Rules".

## Who uses it

- `api` resolves each node's class, key and operation against the set at compile time (phase R5).
- The distributed layer picks a worker with the same `resolve()` (phase R10).
