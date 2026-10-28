# buffer_exchange tests

The tests of the `buffer_exchange` module, mirroring `modules/extension/buffer_exchange/`.
`tests/extension/buffer_exchange/CMakeLists.txt` builds them into `sr_buffer_exchange_test_objects`, linked into `sr_extension_tests`. `sr
test` runs them with the rest; `sr run sr_extension_tests -- --gtest_filter='<pattern>'` runs
one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_buffer_exchange.cpp

The tests drive `BufferExchange` over a `BufferRegistry` with no socket and no thread. Buffers are
host vectors registered by pointer, each followed by one guard byte that a write past the end would
change. A pull is read the way a peer's writer reads it: one `OutboundFrame`, reused for every
`next()`. Every exchange uses the smallest chunk, 4 KiB.

### `ChunkedRoundTrip`

Runs at 0, 1, chunk - 1, chunk, chunk + 1 and 3 chunks + 7 bytes. It pulls a registered buffer,
decodes each frame with `BufferChunkMessage::decode`, checks the prefix fields and the offset, and
compares the reassembled bytes with the source. The same chunks then go back as task-output chunks
into a second buffer, which must end up equal to the source, guard byte included. `finish_task`
answers true once and false the second time, because the first call forgets the task.

| Test | What it proves |
| --- | --- |
| `EveryStreamEndsWithALastChunk` | At each of the six sizes only the final frame carries the last-chunk flag and only it comes back as `FinalFrame`. A 0-byte buffer is one empty chunk with the flag. The call after the final frame returns `Finished` and leaves the frame's payload as it was. |

### `ForgedVersionFenced`

Pins the second half of the output fence (finding DIST-03). A chunk one version above the buffer's
and one a version below are both `Stale` and change no byte. After an honest first chunk the
registry's version is raised, as a reaper does before it resubmits, and the task's second chunk is
`Stale` as well. `stale_writebacks` counts three.

| Test | What it proves |
| --- | --- |
| `GapDiscardsTransfer` | After piece 0, piece 2 is a `Gap`. Piece 1, which was the expected one, is then a `Gap` too: the transfer's progress is gone. A transfer that starts again at offset 0 runs to `Completed`. |
| `DuplicateChunkDiscardsTransfer` | Piece 1 sent twice is a `Gap` the second time, and piece 2 after it is refused as well. |

### `ReleaseDuringStreamAborts`

Pins finding N1. The buffer is unregistered and its allocation freed between two `next()` calls. The
stream's next frame is `BUFFER_ABORT` with reason `Gone`, and it is the last. The registry copied
nothing for it, so an ASan run reports no read of the freed allocation; without ASan the test only
proves the abort. A pull of an id that was never shared is one `BUFFER_ABORT` frame.

| Test | What it proves |
| --- | --- |
| `StaleVersionAbortsThePull` | The buffer's version is raised between two `next()` calls: the stream ends with `BUFFER_ABORT`, reason `Stale`. A request that names a version the buffer has already left gets that frame alone. |
| `MalformedChunkIsRefusedBeforeAnyWrite` | A chunk with the pull-reply purpose, one that declares a size other than the buffer's, one that runs past the end, and one flagged last before the end are each `Malformed`. The buffer keeps its bytes. |

### `ForgottenTaskStartsOver`

After `forget_task` the task's next chunk is a `Gap`. A chunk that continues a transfer from another
connection than the one that started it is a `Gap` as well.

## test_buffer_registry.cpp

The tests cover `BufferRegistry` alone, on host vectors registered by pointer, with one thread. No
test here proves that `unregister_buffer` waits for a copy in progress: that needs a way to stop a
thread inside `read()`, which the registry does not offer.

| Test | What it proves |
| --- | --- |
| `RegisteringASharedAddressKeepsItsHandle` | The first id is 1 and the first version is 1. Registering the same address again returns the same id and the same version; a second address gets id 2. |
| `BumpRaisesTheVersionAndSkipsAGoneID` | `bump` writes the raised version into each handle and leaves a handle whose id is gone as it was. `bump_addresses` raises the version of a shared address and ignores an address that is not shared. |
| `ResolveAndRefreshFollowTheCurrentVersion` | `resolve` returns handles in the order of the addresses and false when one is not shared. `refresh` rewrites each handle's version and returns false once an id has been unregistered. |

### `ReadAtAnOffsetCopiesExactlyTheChunk`

A 16-byte read at offset 10 lands in the middle of a guarded destination; the guard bytes on both
sides stay. A read that ends on the buffer's last byte succeeds. One byte further, an offset beyond
the size, and an offset of 2^64 - 1 are `OutOfRange`; another version is `Stale`; an unknown id is
`Gone`. None of the refused reads changes the destination. A 0-byte read at the end only checks.

| Test | What it proves |
| --- | --- |
| `WriteAtAnOldVersionIsRefused` | After `bump`, a write at the version the task was dispatched with is `Stale`, as is one at a version the buffer has not reached, and neither changes a byte. A write at the current version lands at its offset only and leaves the version as it was. |
| `UnregisterReturnsTheID` | `unregister_buffer` returns the id of a shared address and an invalid id for an address that was never shared or was already released. Lookups by address and by id then find nothing, and `read` and `write` report `Gone`. The same address registered again gets a new id. |
