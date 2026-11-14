# work_stealing tests

The tests of the `work_stealing` module, mirroring `modules/engine/work_stealing/`.
`tests/engine/work_stealing/CMakeLists.txt` builds them into `sr_work_stealing_test_objects`, linked into `sr_engine_tests`. `sr test`
runs them with the rest; `sr run sr_engine_tests -- --gtest_filter='<pattern>'` runs one
program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_mpsc_ring.cpp

The tests move `std::uint32_t` values. `Small_MPSCRing` runs the bounded ring contract of
`tests/support/bounded_ring_contract.hpp` on an `MPSCRing` of capacity 4. The medium test hands
values between threads through a ring of capacity 64, far below what is pushed, so the ring fills
and wraps many times; each producer's values encode the producer and the index, so the consumer
checks every producer's order.

| Test | What it proves |
| --- | --- |
| `Small_MPSCRing/BoundedRingContract/0.OneProducersValuesComeOutInOrder` | Values come out in the order one producer pushed them. |
| `Small_MPSCRing/BoundedRingContract/0.AnEmptyRingPopsNothing` | A pop from an empty ring returns false and leaves the output untouched. |
| `Small_MPSCRing/BoundedRingContract/0.AFullRingRefusesAPushAndKeepsItsValues` | A push into a full ring returns false, leaves the value with the caller and keeps the values held. |
| `Small_MPSCRing/BoundedRingContract/0.APopFreesASlotOfAFullRing` | One pop from a full ring lets the next push in, behind the values already held. |
| `Small_MPSCRing/BoundedRingContract/0.ValuesKeepTheirOrderAcrossWraps` | Ten laps of filling and draining return every value in order, across the wrap of the slot index. |
| `Medium_MPSCRing.ManyProducersHandEveryValueOverOnceAndInTheirOrder` | Four producer threads push 20000 values each; the consumer receives every value once, each producer's in its order. |

## test_spsc_ring.cpp

`Small_SPSCRing` runs the same bounded ring contract on an `SPSCRing` of capacity 4; its five rows
prove what the rows of `test_mpsc_ring.cpp` above prove.

| Test | What it proves |
| --- | --- |
| `Small_SPSCRing/BoundedRingContract/0.*` | The five contract promises listed under `test_mpsc_ring.cpp`, on an `SPSCRing`. |
| `Medium_SPSCRing.AProducerThreadHandsEveryValueOverOnceAndInOrder` | One producer thread hands 20000 values to the consumer once each and in order. |

## test_work_stealing_deque.cpp

The tests push tags, never real tasks: the deque moves pointers and never reads through them.

| Test | What it proves |
| --- | --- |
| `Small_WorkStealingDeque.PushBeyondCapacityParksOverflow` | A push past the capacity parks the task in the overflow list; nothing is dropped. A builder pushes the tags. |
| `Small_WorkStealingDeque.PopDrainsOverflowExactlyOnce` | The owner's pops, on one thread, return every task of the ring and the overflow once each. |
| `Medium_WorkStealingDeque.NoLossUnderConcurrentSteal` | While the owner pushes and pops, three thieves steal; every task comes out exactly once. The owner and thief loops are named helpers. |
