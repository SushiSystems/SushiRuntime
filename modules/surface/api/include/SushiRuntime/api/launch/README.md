# api/launch

The iteration geometry of the fluent API: Extent and NdRange.

The sections below record why the code is the way it is. Each one is named after a header and the
symbol or passage it explains, and that header's file block cites this page.

## The launch descriptors

A launch says how many work-items a kernel runs over and what index each one receives. It is the
first argument of add() and add_untracked(), before the data the kernel names.

| Launch | Work-items | Index the kernel receives |
| --- | --- | --- |
| an element count `n` | `n` | `std::size_t` in [0, n) |
| `Extent<N>` | the product of `extent` | `sycl::id<N>`, with `origin` added |
| `NdRange<N>` | the product of `global` | `sycl::nd_item<N>`; a second `sycl::id<N>` parameter gets the global id with `origin` added |
| `Dynamic` and a capacity | bound per step, at most the capacity | `std::size_t` |

Both descriptors name the first index `origin` and both rebase with at(origin), so a caller who
moves a kernel from an Extent to an NdRange keeps the same words.

## extent.hpp

### Extent

Extent pairs a size per dimension, `extent`, with an `origin`, so a kernel iterates the whole
field or a sub-box of it. The sub-box is what lets an interior-only stencil or a halo update be
written without a hand-rolled handler.

A 1-D Extent converts from a plain element count. A plain count is still the shorter spelling for
1-D work: a 1-D Extent is needed only for an origin.

## nd_range.hpp

### NdRange

NdRange gives a kernel the work-group execution model (barriers, local shared memory, sub-group
operations) for tiled work such as a blocked matrix multiply or a shared-memory stencil. The
kernel receives a sycl::nd_item\<N>, as a raw SYCL nd_range parallel_for would hand it.

Each global dimension must be a whole multiple of the matching local dimension, per the SYCL
requirement; the runtime does not pad.

### origin and at

SYCL 2020 deprecates the offset of an nd_range, so to_nd_range() never carries one and the
nd_item a kernel receives always counts from zero. The origin is applied by the launch instead: a
kernel written as `kernel(sycl::nd_item<N> item, sycl::id<N> index)` receives
`item.get_global_id() + origin` as its second argument. A kernel that takes the nd_item alone gets
no shifted id and works out its own position, as it did before the origin existed.

at(origin) returns a copy of the same tiling at another origin, the same call Extent has.
