# api/launch

The iteration geometry of the fluent API: Extent and NdRange.

The sections below record why the code is the way it is. Each one is named after a header and the
symbol or passage it explains, and that header's file block cites this page.

## extent.hpp

### Extent

Extent is the launch vocabulary the multidimensional add() overloads key on. It pairs a global
`extent` (how many work-items per dimension) with an `offset` origin, so a kernel can iterate the
whole field or just a sub-box of it. The sub-box is what makes interior-only stencils and halo or
ghost-layer updates expressible without a hand-rolled handler.

A 1-D Extent is implicitly constructible from a plain element count, so every existing
add(std::size_t, fn) call site keeps compiling unchanged and 1-D stays the zero-ceremony default.

## nd_range.hpp

### NdRange

NdRange unlocks the work-group execution model (barriers, local shared memory and sub-group
operations) for callers who need tiled kernels such as a blocked matrix multiply or a shared-memory
stencil. The fluent overload that accepts one hands the kernel a sycl::nd_item\<N>, exactly as a raw
SYCL nd_range parallel_for would.

Each global dimension must be a whole multiple of the matching local dimension, per the SYCL
requirement; the runtime does not pad.
