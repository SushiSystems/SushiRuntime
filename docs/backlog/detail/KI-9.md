# KI-9 Any host that reaches the master's port can hold every peer slot

The row of this entry is in `docs/backlog/RUNTIME.md`. This file holds what did not fit the row.

- Origin: own
- Where: `modules/extension/distributed/source/tcp_transport.cpp`
- Cause: every open connection counts towards `max_peers`, registered or not
- Recognise: registered workers are refused while unregistered connections stay open
- Workaround: keep the port behind a firewall
- Fix: RW-19
