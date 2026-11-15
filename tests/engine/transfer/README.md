# transfer tests

The tests of the `transfer` module, mirroring `modules/engine/transfer/`.
`tests/engine/transfer/CMakeLists.txt` builds them; `sr test` runs them with the rest.

## test_same_context_path.cpp

The path connects spaces of one context and no others, copies the bytes on the CPU queue, and tells
its observer the name `same_context`. The tests skip when SYCL offers no CPU device.

## test_peer_path.cpp

The path connects two contexts only when access is granted and leaves one context to
`SameContextPath`. It copies the bytes and names itself `peer`. A plan over both paths takes
`same_context` for the copy inside a context and `peer` for the copy across contexts; the planned
copy nodes move the bytes, and the observer records which path each copy took.

## test_host_staged_path.cpp

The path connects any two devices and the host with a device, never two host spaces. A copy
between two spaces with no direct path matches bytes for sizes below, at and several times one ring
slot, makes the ring once and reuses it, and the report names `host_staged` once per copy. A copy to
or from the host is one memcpy with no slot. A copy with no queue on either side returns
`InvalidArgument`.

## test_staging_ring.cpp

The ring makes no slot before the first acquire, makes all slots once, hands them out in ring
order, skips a held slot, frees every slot with the ring, and returns `AllocationFailed` when the
memory has no room, after it released the slots it had made.

## test_transfer_observer_slot.cpp

An empty slot drops every copy, an installed observer hears each copy in order, and installing null
stops the forwarding.

## test_transfer_cost.cpp

The uniform cost prices every path by its bytes.

## test_file_path.cpp

`Small_FilePath` writes a temporary file and checks that `FilePath` connects an added file space to
any space that is not a file, streams ranges onto a SYCL CPU device through a two-slot ring with
equal bytes and a peak of pinned memory equal to the ring, reads straight into the host with no
slot, and returns `InvalidArgument` for a space it does not hold and `InvalidState` for a range past
the end of the file. After `remove()` the
path does not connect the removed space, and a new space added under the same id is connected.
`Large_FilePath` writes a file 1 GiB larger than the first CUDA GPU's memory into `temp` under the
working directory, streams it window by window onto the GPU, checks the first and last 4 KiB of each
window, and checks that pinned memory stays within the ring, under the ring plus 512 MB. It skips
with no CUDA GPU or too little disk, and deletes the file.

## compile_peer_access.cpp, compile_space_queues.cpp, compile_transfer_log_category.cpp, compile_transfer_observer.cpp and compile_transfer_path.cpp

Each file includes one header alone.
