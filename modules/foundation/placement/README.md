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
| `placement_rules.hpp` | `PlacementRules`: `add()` checks and keeps a rule, `resolve()` returns the position of the winning rule, `rule()`, `size()` and `empty()` read the set, and `K_MAX_DURATION` is the longest `Wait` bound and deadline `add()` accepts |
| `placement_target_matching.hpp` | `device_on()`, `machine_matches()`, `pick_device()`, `machine_names_match()`, `describe_rule()`, `describe_target()` and `K_DEFAULT_PLACEMENT_DEVICE`: a target read against the `NodeFacts` of one machine |

Every name is declared in `SushiRuntime`. `PlacementRules` and the six functions are exported
with `SUSHIRUNTIME_API` and built from `source/placement_rules.cpp` and
`source/placement_target_matching.cpp`.

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
| Policy | a `Wait` bound or a `deadline` that is not positive, or above `K_MAX_DURATION` |

An exact class of any depth is accepted, since `PlacementClass` compares it by its whole-name hash.

`K_MAX_DURATION` is one day. A wait or a deadline longer than a day is not a placement any run
needs, so a larger value is taken for a mistake, such as seconds written where milliseconds were
meant. A day added to any `steady_clock` reading also stays far below the clock's range, so the
end of a wait never overflows; `RulePlacement` saturates the sum anyway.

## placement_rules.hpp

### The precomputed form

`add()` stores each rule in a private `Entry` beside what `resolve()` compares: how the class is
matched, the hash of the class or prefix, and the `OperationID` of the operation. `resolve()` reads
only those hashes and the priority, so it never touches a string. The design is in
[`docs/design/LAYERED_EXECUTION/README.md`](../../../docs/design/LAYERED_EXECUTION/README.md),
section "Rules".

## placement_target_matching.hpp

`device_on()` answers where a target puts a node on one machine, and every caller that places by a
rule asks it: the compile of a `Graph` and of a `DynamicGraph`, `PlacementScope::device()`,
`RulePlacement` for the master and `WorkerDirectory::pick_target()` for each worker. A target
names a machine only when both of its parts hold there: `machine_matches()` accepts the machine
part, and `pick_device()` finds a device for the device part. So a target with only a device part
names every machine that has such a device and no other: a GPU rule does not name a machine with
no GPU, and a rule for device 1 does not name a machine with one device. `device_on()` returns
the position in `NodeFacts::devices`, or none.

`machine_matches()` compares the machine part: a `machine_name` matches `NodeFacts::name` by
`machine_names_match()`, a `machine_label` is one of the machine's labels with that key and that
value, and a target with no machine part matches every machine. `pick_device()` reads the device
part alone: the device at `device_index`, the first device of `device_kind`, or the first device
with at least `device_min_memory` bytes; it returns none when the machine has no such device, and
`K_DEFAULT_PLACEMENT_DEVICE` (0) for a target with no device part. `describe_rule()` and
`describe_target()` give the phrases an error names a rule and its target with: the rule's name in
quotes, else `at position N`.

### Machine names

A machine name is compared in one canonical form: its first DNS label, the text before the first
`.`, in ASCII lower case. `machine_names_match()` compares two names that way without allocating,
and it applies to a rule's `machine_name` and to the name a machine reports alike, so `Rack-7`,
`rack-7` and `rack-7.lab.example.com` are one machine.

The two platforms report a host differently. Windows gives `ComputerNameDnsHostname`, a single
label often in capitals, and POSIX `gethostname()` often gives the fully qualified name. DNS
compares names without regard to case, and the first label is the part both platforms report, so
that form makes one machine compare equal from either side. Two machines whose names differ only
after the first dot compare equal; a cluster names its machines by host name, not by address, and
gives each a first label of its own.

## Who uses it

- `api` resolves each node's class, key and operation against the set when a `Graph` compiles or
  a `DynamicGraph` composes its regions, and writes the device `device_on()` gives on
  `Runtime::topology()` into the node's stated placement unless the node states one; a placement
  scope reads its rule's target the same way (phases R5, R6, R16a).
- `offload_coordination` reads a rule's target with `device_on()` against each worker's facts and
  against the master's own facts (phases R10, R16a).
