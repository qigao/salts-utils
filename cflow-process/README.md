# Process

`Salts::Process` combines the low-level process owner from installed Salts with
CFlow's bounded native byte-pipe Actor. The adapter is owned by SaltsUtils;
there is no `Salts::CFlowProcess` compatibility target.

The adapter owns one `salts_process_t`, three parent-side asynchronous pipe
endpoints, one fixed-capacity native backend, one manual Executor, one IO Actor,
and exactly `request_capacity` operation slots. It never exposes raw endpoints,
captures output into an unbounded buffer, or creates a second process state
machine.

Public C API remains `cflow_process_*`, but the header is now:

```c
#include <salts/process.h>
```

Consumers link:

```cmake
find_package(SaltsUtils CONFIG REQUIRED)
target_link_libraries(app PRIVATE Salts::Process)
```

All admission, borrowed-buffer, cancellation, close, termination, callback,
quiescence, and destruction semantics are unchanged from the previous adapter.
Unsupported native pipe backends return `SALTS_ENOTSUP`; there is no fallback.
