# Filesystem

`Salts::FS` is the SaltsUtils filesystem control-plane adapter between the
synchronous `salts_fs` implementation and CFlow's bounded execution model.
It is not part of the Salts core package, and there is no `Salts::CFlowFS`
compatibility target.

Public headers are:

```c
#include <salts/fs.h>
#include <salts/fs_watch.h>
#include <salts/fs_watch_publisher.h>
```

Consumers use:

```cmake
find_package(SaltsUtils CONFIG REQUIRED)
target_link_libraries(app PRIVATE Salts::FS)
```

The `cflow_fs_*` C API, bounded worker-backed filesystem service, native
filesystem watcher behavior, loss/rescan protocol, Publisher ownership and
platform backend semantics are preserved from the former adapter. The move
changes package ownership, public target identity and public header placement
only. No fallback or compatibility header is provided.
