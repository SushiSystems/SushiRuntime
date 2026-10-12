# distributed tests

The tests of the `distributed` module, built into one program, `sr_distributed_tests`, which
`modules/extension/distributed/CMakeLists.txt` declares. `sr test` runs it with the rest;
`sr run sr_distributed_tests -- --gtest_filter='<pattern>'` runs it alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

A test file is in the folder named after its subject, and the `README.md` of that folder holds
its section.

| Folder | Tests | Notes |
| --- | --- | --- |
| `wire/` | The frame, the codec of every message and the offload policy | [wire/README.md](wire/README.md) |
| `link/` | What carries a frame between two machines: the socket, the handshake and the transports | [link/README.md](link/README.md) |
| `buffers/` | The buffer registry and the chunked exchange of a buffer | [buffers/README.md](buffers/README.md) |
| `coordinator/` | The master's side: the worker directory, the pending tasks and the offload dispatcher | [coordinator/README.md](coordinator/README.md) |
| `worker/` | The worker's side: its bricks, its options and its process | [worker/README.md](worker/README.md) |
| `end_to_end/` | A master and its workers together, with and without faults on the link | [end_to_end/README.md](end_to_end/README.md) |
