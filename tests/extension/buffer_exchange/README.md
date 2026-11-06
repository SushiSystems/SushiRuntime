# buffer_exchange tests

The tests of the `buffer_exchange` module, mirroring `modules/extension/buffer_exchange/`.
`tests/extension/buffer_exchange/CMakeLists.txt` builds them into `sr_buffer_exchange_test_objects`, linked into `sr_extension_tests`. `sr
test` runs them with the rest; `sr run sr_extension_tests -- --gtest_filter='<pattern>'` runs
one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_buffer_exchange.cpp

The tests drive `BufferExchange` over a `BufferRegistry` with no socket and no thread, so every test is
small. Buffers are host vectors registered by pointer, each followed by one guard byte that a write past
the end would change. A pull is read the way a peer's writer reads it: one `OutboundFrame`, reused for
every `next()`. Every exchange uses the smallest chunk, 4 KiB.

### `Small_BufferExchangeBoundary`

Runs each test at 0, 1, chunk - 1, chunk, chunk + 1 and 3 chunks + 7 bytes.

| Test | What it proves |
| --- | --- |
| `ChunkedRoundTrip` | A pulled buffer decodes frame by frame with the right prefix fields and offsets, and reassembles to the source. The same chunks sent back as task-output chunks fill a second buffer equal to the source, guard byte included. `finish_task` answers true once and false the second time. |
| `EveryStreamEndsWithALastChunk` | Only the final frame carries the last-chunk flag and only it comes back as `FinalFrame`. A 0-byte buffer is one empty chunk with the flag. |
| `ACallAfterTheFinalFrameFinishesAndLeavesTheFrameAlone` | The call after the final frame returns `Finished` and leaves the frame's payload as it was. |

### Output fence

Pins the second half of the output fence (finding DIST-03).

| Test | What it proves |
| --- | --- |
| `Small_BufferExchangeForgedVersion.ChunkIsStaleAndWritesNothing` | A chunk one version above the buffer's, and one a version below, are each `Stale` and change no byte. |
| `ABumpWhileAChunkIsInFlightMakesItStale` | After an honest first chunk the registry's version is raised, as a reaper does before it resubmits, and the task's second chunk is `Stale`. |
| `Small_BufferExchangeMalformedChunk.IsRefusedBeforeAnyWrite` | A chunk with the pull-reply purpose, one that declares a size other than the buffer's, one that runs past the end, and one flagged last before the end are each `Malformed`. The buffer keeps its bytes. |

### Transfer progress

| Test | What it proves |
| --- | --- |
| `AGapRefusesTheMissingPieceAndTheOneAfterIt` | After piece 0, piece 2 is a `Gap`. Piece 1, which was the expected one, is then a `Gap` too: the transfer's progress is gone. |
| `ATransferRestartsFromPieceZeroAfterAGap` | A transfer that starts again at offset 0 runs to `Completed`, and the buffer equals the payload. |
| `ADuplicateChunkDiscardsTheTransfer` | Piece 1 sent twice is a `Gap` the second time, and piece 2 after it is refused as well. |
| `ForgettingATaskDiscardsItsTransfer` | After `forget_task` the task's next chunk is a `Gap`. |
| `AContinuationFromAnotherConnectionIsRefused` | A chunk that continues a transfer from another connection than the one that started it is a `Gap`. |

### Aborted pulls

| Test | What it proves |
| --- | --- |
| `ReleasingABufferDuringAStreamAbortsItWithGone` | Finding N1. The buffer is unregistered and its allocation freed between two `next()` calls. The stream's next frame is `BUFFER_ABORT` with reason `Gone`, and it is the last. The registry copied nothing for it, so an ASan run reports no read of the freed allocation; without ASan the test only proves the abort. |
| `APullOfAnUnknownBufferIsRefusedWithGone` | A pull of an id that was never shared is one `BUFFER_ABORT` frame. |
| `ABumpDuringAStreamAbortsItWithStale` | The buffer's version is raised between two `next()` calls: the stream ends with `BUFFER_ABORT`, reason `Stale`. |
| `APullNamingAVersionAlreadyLeftGetsTheAbortAlone` | A request that names a version the buffer has already left gets that frame alone. |

## test_buffer_registry.cpp

The tests cover `BufferRegistry` alone, on host vectors registered by pointer, with one thread, so every
test is small. No test here proves that `unregister_buffer` waits for a copy in progress: that needs a
way to stop a thread inside `read()`, which the registry does not offer.

| Test | What it proves |
| --- | --- |
| `RegisteringASharedAddressAgainKeepsItsHandle` | Registering the same address again returns the same id and the same version. |
| `DistinctAddressesGetDistinctIDs` | A second address gets an id of its own and its own size. |
| `BumpRaisesTheVersionOfALiveHandleAndLeavesAGoneOne` | `bump` writes the raised version into each live handle and leaves a handle whose id is gone as it was. |
| `BumpAddressesRaisesTheVersionOfASharedAddress` | `bump_addresses` raises the version of a shared address and ignores an address that is not shared. |
| `ResolveMapsAddressesToHandlesInTheirOrder` | `resolve` returns handles in the order of the addresses. |
| `ResolveRefusesAnAddressThatIsNotShared` | `resolve` returns false when one address is not shared. |
| `RefreshRewritesEachHandleToTheCurrentVersion` | `refresh` rewrites each handle's version. |
| `RefreshRefusesAHandleWhoseIDWasUnregistered` | `refresh` returns false once an id has been unregistered. |
| `ReadCopiesExactlyTheRequestedChunk` | A 16-byte read at offset 10 lands in the middle of a guarded destination; the guard bytes on both sides stay. |
| `ReadEndingOnTheLastByteSucceeds` | A read that ends on the buffer's last byte succeeds. |
| `ReadOfNothingAtTheEndOnlyChecks` | A 0-byte read at the end succeeds. |
| `Small_BufferRegistryRefusedRead.ReturnsItsStatusAndCopiesNothing` | One byte past the end, an offset beyond the size, and an offset of 2^64 - 1 are `OutOfRange`; another version is `Stale`; an unknown id is `Gone`. The destination keeps its guard bytes. |
| `Small_BufferRegistryRefusedWrite.ReturnsItsStatusAndWritesNothing` | After `bump`, a write at the version the task was dispatched with is `Stale`, as is one at a version the buffer has not reached; a write past the end is `OutOfRange`. None changes a byte. |
| `WriteAtTheCurrentVersionLandsAtItsOffsetOnly` | A write at the current version lands at its offset and leaves the bytes around it. |
| `WriteLeavesTheVersionAsItWas` | A landed write does not raise the buffer's version. |
| `UnregisterReturnsTheIDOfASharedAddressOnce` | `unregister_buffer` returns the id of a shared address, and an invalid id for an address that was never shared or was already released. |
| `AnUnregisteredBufferIsGone` | Lookups by address and by id find nothing, and `read` and `write` report `Gone`. |
| `AnAddressSharedAgainAfterReleaseIsANewBuffer` | The same address registered again gets a new id and starts at version 1. |
