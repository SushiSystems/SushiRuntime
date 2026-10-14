# launch tests

## test_extent.cpp

The file tests Extent: the count of each rank, the origin and at().

| Test | What it proves |
| --- | --- |
| `CountIsTheProductOfTheSizes` | Checks the count of a one-, a two- and a three-dimensional extent. |
| `AtMovesTheOrigin` | At() keeps the sizes and moves the origin. |

## test_nd_range.cpp

The file tests NdRange: the global count, the SYCL range it builds and at().

| Test | What it proves |
| --- | --- |
| `CountIsTheGlobalSize` | The count is the global size and the SYCL range keeps both sizes. |
| `AtMovesTheOrigin` | At() keeps both sizes and moves the origin. |
