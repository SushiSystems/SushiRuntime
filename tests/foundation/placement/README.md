# placement tests

The tests of the `placement` module, mirroring `modules/foundation/placement/`.
`tests/foundation/placement/CMakeLists.txt` builds them into `sr_placement_tests`. `sr test` runs
them with the rest; `sr run sr_placement_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_placement_rule.cpp

The file tests PlacementRule and its records: the values they hold before a caller fills them.

| Test | What it proves |
| --- | --- |
| `Small_AbsentPolicy.DefaultsRunLocallyWithNoBound` | Checks that a default policy is `Local` with a zero bound. |
| `Small_PlacementMatch.DefaultsStateNothing` | Checks that a default match has no class, key or operation. |
| `Small_PlacementTarget.DefaultsNameNothing` | Checks that a default target names no machine and no device. |
| `Small_PlacementRule.DefaultsHoldNoNamePriorityZeroAndALocalPolicy` | Checks the defaults of the rule. |

## test_placement_rules.cpp

The file tests PlacementRules: the match, the winner and the refusals of `add()`.

| Test | What it proves |
| --- | --- |
| `Small_PlacementRules.AnEmptySetHasNoRuleAndResolvesNothing` | Checks that an empty set resolves no node. |
| `Small_PlacementRules.RuleReturnsTheRulesInInsertionOrder` | Checks that `rule()` returns the rules in the order they were added. |
| `Small_PlacementRules.AnExactClassOfAnyDepthIsAccepted` | Checks that an exact class deeper than the prefix limit is accepted and matched. |
| `Small_PlacementRules.ResolveTableMatchesClassPrefixKeyOperationAndPriority` | Resolves a table of nodes: exact class, prefix, key, operation, a rule for any class, two conflicting rules, equal priorities and a node no rule matches. |
| `Small_PlacementRules.AddRefusesEveryCaseOfTheRefusedWhenColumn` | Checks that `add()` refuses each case of the design table with `InvalidArgument`, a message that names the rule and says what to pass, and keeps nothing. |
| `Small_PlacementRules.ARefusalNamesAnUnnamedRuleByItsPosition` | Checks that a rule without a name is named by its position. |
| `Small_PlacementRules.APositiveWaitBoundAndDeadlineAreAccepted` | Checks that a positive `Wait` bound and deadline pass. |
| `Small_PlacementRules.Regression_AWaitBoundOrDeadlineAboveTheMaximumIsRefused` | Checks that a `Wait` bound or a deadline above `PlacementRules::K_MAX_DURATION`, `milliseconds::max()` among them, is refused with `InvalidArgument`, and that the maximum itself passes. |

## test_placement_target_matching.cpp

The file tests the match of a target against one machine's facts: a machine "rack-7" in zone `a`
with a CPU, then a GPU.

| Test | What it proves |
| --- | --- |
| `Small_PlacementTargetMatching.MachinePartMatchesByNameOrLabel` | A target with no machine part matches; a name or a label matches its machine and no other. |
| `Small_PlacementTargetMatching.DevicePartPicksByIndexKindOrMemory` | No device part picks 0; an index, a kind and a least memory each pick their device, and none when the machine lacks it. |
| `Small_PlacementTargetMatching.DescriptionsNameTheRuleAndEveryTargetPart` | A rule is named by its name in quotes, else by its position; a target's phrase lists each part it states. |
| `Small_PlacementTargetMatching.Regression_ADeviceOnlyTargetNamesOnlyAMachineWithTheDevice` | `device_on()` gives none for a GPU target on a CPU-only machine and for device 1 of a one-device machine, gives the device on a machine that has it, and gives none when the machine part names another machine. |
| `Small_PlacementTargetMatching.Regression_MachineNamesCompareByCaseAndFirstLabel` | `machine_names_match()` and `machine_matches()` treat `Rack-7`, `RACK-7` and `rack-7.lab.example.com` as one machine, and `rack-70` and `lab.rack-7` as others. |
