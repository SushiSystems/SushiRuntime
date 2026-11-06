# placement tests

The tests of the `placement` module, mirroring `modules/foundation/placement/`.
`tests/foundation/placement/CMakeLists.txt` builds them into `sr_placement_test_objects`, linked into `sr_foundation_tests`. `sr test` runs
them with the rest; `sr run sr_foundation_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_placement_rule.cpp

The file tests PlacementRule: a rule that states no policy runs its node locally.

| Test | What it proves |
| --- | --- |
| `Small_AbsentPolicy.ARuleThatStatesNoPolicyRunsLocally` | A rule added with no stated absent policy is kept with the `Local` action. |

## test_placement_rules.cpp

The file tests PlacementRules: the match, the winner and the refusals of `add()`.

| Test | What it proves |
| --- | --- |
| `Small_PlacementRules.AnEmptySetHasNoRuleAndResolvesNothing` | Checks that an empty set resolves no node. |
| `Small_PlacementRules.RuleReturnsTheRulesInInsertionOrder` | Checks that `rule()` returns the rules in the order they were added. |
| `Small_PlacementRules.AnExactClassOfAnyDepthIsAccepted` | Checks that an exact class deeper than the prefix limit is accepted and matched. |
| `Small_PlacementRules.AddRefusesEveryCaseOfTheRefusedWhenColumn` | Checks that `add()` refuses each case of the design table with `InvalidArgument`, a message that names the rule and says what to pass, and keeps nothing. |
| `Small_PlacementRules.ARefusalNamesAnUnnamedRuleByItsPosition` | Checks that a rule without a name is named by its position. |
| `Small_PlacementRules.APositiveWaitBoundIsAccepted` | A `Wait` with a positive bound passes. |
| `Small_PlacementRules.APositiveDeadlineIsAccepted` | A positive deadline passes. |
| `Small_PlacementRules.Regression_AWaitBoundAboveTheMaximumIsRefused` | A `Wait` bound above `PlacementRules::K_MAX_DURATION`, `milliseconds::max()` among them, is refused with `InvalidArgument`. |
| `Small_PlacementRules.Regression_ADeadlineAboveTheMaximumIsRefused` | A deadline above `PlacementRules::K_MAX_DURATION` is refused with `InvalidArgument`. |
| `Small_PlacementRules.TheMaximumIsAcceptedAsAWaitBoundAndAsADeadline` | `K_MAX_DURATION` itself passes as a bound and as a deadline. |

### `Small_PlacementRulesResolve`

The fixture adds ten rules once, in a fixed order, so the positions the tests expect are fixed: exact class, prefix, keyed,
operation, any-class key, two conflicting classes, two equal priorities and a key 0 rule. Each test passes a table of nodes to
one helper.

| Test | What it proves |
| --- | --- |
| `ClassPatternsMatchExactAndPrefix` | An exact class matches itself, a prefix matches the classes below it and never its own, and a node no rule matches resolves to none. |
| `AKeyRuleMatchesOnlyANodeThatCarriesTheKey` | A key rule matches any class, key 0 is a key like any other, and a node without a key never matches. |
| `AnOperationRuleMatchesItsOperationUnderAnyClass` | An operation rule matches its operation under any class and under none. |
| `TheLargerPriorityWinsAndATieGoesToTheFirstAdded` | The larger priority wins a conflict, and equal priorities go to the rule added first. |

## test_placement_target_matching.cpp

The file tests the match of a target against one machine's facts: a machine "rack-7" in zone `a`
with a CPU, then a GPU.

| Test | What it proves |
| --- | --- |
| `Small_PlacementTargetMatching.MachinePartMatchesByNameOrLabel` | A target with no machine part matches; a name or a label matches its machine and no other. |
| `Small_PlacementTargetMatching.NoDevicePartPicksTheFirstDevice` | A target with no device part picks device 0. |
| `Small_PlacementTargetMatching.DeviceIndexPicksThatDevice` | An index picks that device, and none when the machine has fewer. |
| `Small_PlacementTargetMatching.DeviceKindPicksTheFirstDeviceOfThatKind` | A kind picks the first device of that kind, and none when the machine has none. |
| `Small_PlacementTargetMatching.DeviceMemoryPicksTheFirstDeviceWithEnough` | A least memory picks the first device that has it, and none when no device does. |
| `Small_PlacementTargetMatching.DescribeRuleNamesTheRuleByNameElseByPosition` | A rule is named by its name in quotes, else by its position. |
| `Small_PlacementTargetMatching.DescribeTargetNamesEveryPartItStates` | A target's phrase lists each part it states. |
| `Small_PlacementTargetMatching.Regression_ADeviceOnlyTargetNamesOnlyAMachineWithTheDevice` | `device_on()` gives none for a GPU target on a CPU-only machine and for device 1 of a one-device machine, and gives the device on a machine that has it. |
| `Small_PlacementTargetMatching.Regression_ADeviceOnAMachineNeedsTheMachinePartToMatch` | `device_on()` gives none when the machine part names another machine. |
| `Small_PlacementTargetMatching.Regression_MachineNamesCompareByCaseAndFirstLabel` | `machine_names_match()` treats `Rack-7`, `RACK-7` and `rack-7.lab.example.com` as one machine, and `rack-70` and `lab.rack-7` as others. |
| `Small_PlacementTargetMatching.Regression_MachineMatchesComparesATargetNameLikeMachineNamesMatch` | `machine_matches()` applies the same comparison to a target's name and a machine's name. |
