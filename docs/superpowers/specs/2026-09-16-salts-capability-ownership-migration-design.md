# Salts capability ownership migration design

**Date:** 2026-09-16

## Goal

Move the higher-level filesystem, process, and crypto capabilities out of
`qigao/salts` and make `qigao/salts-utils` their only owner while keeping the
low-level Salts primitives in `qigao/salts`.

The migrated public CMake targets are:

- `Salts::Crypto`
- `Salts::FS` (replaces `Salts::CFlowFS`)
- `Salts::Process` (replaces `Salts::CFlowProcess`)

The migration is a clean cutover. The old `Salts::CFlowFS` and
`Salts::CFlowProcess` targets are removed. No aliases, compatibility targets,
fallback package lookup, source-directory fallback, or dual ownership are
allowed.

## Ownership boundary

After the migration, `qigao/salts` remains the owner of the low-level runtime
and primitives consumed by the moved capabilities:

- `Salts::Platform`
- `Salts::Core`
- `Salts::CFlow`
- synchronous filesystem primitives declared by the existing Salts filesystem
  API
- process ownership/spawn primitives declared by the existing Salts process API

`qigao/salts-utils` becomes the only owner of:

- crypto: Ed448 and SHA-256 public API and its private libecc implementation
- filesystem service, native filesystem watcher, and typed filesystem watcher
  publisher
- subprocess asynchronous stdin/stdout/stderr adapter

The dependency direction is strictly:

```text
application
    |
    v
salts-utils
    +-- Salts::Crypto  ------> Salts::Platform
    +-- Salts::FS      ------> Salts::Core / Salts::CFlow / Salts::Platform
    +-- Salts::Process ------> Salts::Core / Salts::CFlow / Salts::Platform
    |
    v
installed Salts package
```

`salts-utils` must not include Salts source-tree private headers or add Salts
source directories to its build.

## Public header layout

Public headers for the moved CFlow adapters must no longer live under
`include/cflow`.

The destination layout is:

```text
salts-utils/
  crypto/
    include/salts/crypto.h
  cflow-fs/
    include/salts/fs.h
    include/salts/fs_watch.h
    include/salts/fs_watch_publisher.h
  cflow-process/
    include/salts/process.h
```

All production sources, tests, examples, and documentation must include these
headers through `salts/...` paths.

The C function names remain unchanged during this migration:

- `cflow_fs_*`
- `cflow_fs_watch_*`
- `cflow_fs_watch_publisher_*`
- `cflow_process_*`
- `salts_crypto_*`

The function names are intentionally not folded into `salts_fs_*` or
`salts_process_*` because the source repository already owns lower-level
filesystem and process APIs with those semantic namespaces. This migration
changes ownership, public header placement, and CMake target identity, not the
runtime ABI of the adapter functions.

## CMake target contract

The destination build-tree target names may remain implementation-oriented,
for example:

```text
salts_crypto
salts_cflow_fs
salts_cflow_process
```

but their exported public target identities are exactly:

```text
Salts::Crypto
Salts::FS
Salts::Process
```

Accordingly, the destination targets use export names `Crypto`, `FS`, and
`Process` in `SaltsUtilsTargets`.

The source repository must stop exporting all three moved capabilities.
Specifically, `Salts::Crypto`, `Salts::CFlowFS`, and `Salts::CFlowProcess` must
not remain in the Salts package after cutover.

No compatibility aliases for `Salts::CFlowFS` or `Salts::CFlowProcess` are
permitted in either repository.

## Crypto migration

Move the complete crypto capability from `qigao/salts` to `qigao/salts-utils`:

- public `salts/crypto.h`
- Ed448 implementation
- SHA-256 implementation
- tests
- README
- private libecc subtree and its required license material

`Salts::Crypto` continues to depend on `Salts::Platform` for secure randomness.
libecc remains private to the crypto target.

`salts-utils` already owns a private Monocypher target; the migrated crypto
capability uses that local private dependency rather than reaching back into the
Salts source tree.

No crypto behavior, constants, key sizes, signature sizes, SHA-256 semantics,
or error contracts change as part of this migration.

## Filesystem migration

Move the complete current `cflow-fs` implementation and its tests into
`qigao/salts-utils`.

The capability keeps its existing behavioral contracts:

- bounded worker-backed filesystem operations
- copied accepted paths and caller-owned output lifetimes
- cooperative cancellation semantics
- single driver-thread callback delivery
- bounded native watcher queues
- native loss represented by rescan-required state
- typed publisher ownership and close ordering
- platform-specific watcher backends

Its public CMake identity becomes `Salts::FS`.

Its public headers move to `include/salts` and internal includes are updated to
match. The watcher publisher continues to consume the public CFlow reactive API
from installed Salts.

## Process migration

Move the complete current `cflow-process` implementation and tests into
`qigao/salts-utils`.

The capability keeps its current behavioral contracts:

- one process owner
- asynchronous parent-side stdin/stdout/stderr endpoints
- bounded request admission
- borrowed buffer lifetime through terminal callback return
- explicit cancellation and close behavior
- quiescent destruction requirement

Its public CMake identity becomes `Salts::Process` and its public header moves
to `include/salts/process.h`.

The underlying synchronous/native process owner remains in `qigao/salts`.

## Fail-fast package boundary

Package and dependency resolution must fail immediately when the required
ownership boundary is not satisfied.

At minimum, `salts-utils` configuration must stop with `FATAL_ERROR` when:

- `SALTS_ROOT` is missing
- `SALTS_ROOT` is empty or not a directory
- the requested Salts package cannot be found under that exact root
- required imported targets such as `Salts::Platform`, `Salts::Core`, or
  `Salts::CFlow` are absent
- the selected Salts package still exports pre-migration moved targets that
  would conflict with the new ownership

The last condition includes any of:

```text
Salts::Crypto
Salts::CFlowFS
Salts::CFlowProcess
```

being present after the installed Salts package is loaded for a build that owns
the moved targets in `salts-utils`.

There is no fallback to a default package search path, another install prefix,
a source checkout, vendored Salts, or a compatibility target.

## No CMake install-verification framework

Neither repository may add or retain CMake code whose purpose is to install a
build into a staging prefix and then configure/build a synthetic installed
consumer in order to verify the package.

This migration therefore deletes existing install-verification machinery rather
than moving or extending it.

In `qigao/salts`, remove the root-level `verify_installed_package` custom target,
its `VerifyInstalledPackage.cmake` implementation, its install-consumer fixture,
and active README/CMake references that instruct users or CI to invoke that
verification target.

In `qigao/salts-utils`, remove equivalent active install-verification machinery,
including module-specific `VerifyInstalled*.cmake` tests and standalone
install-consumer fixtures such as the current Playback/Jinja package verification
paths. Historical design/plan documents may remain as history, but no live build,
test, preset, or CI path may depend on install verification.

Normal package installation/export remains supported. `install(TARGETS ...)`,
package config generation, and exported target files are not install-verification
machinery and remain part of the package product.

## Testing strategy

Verification is performed with the normal build and normal CTest graph only.
The migration must preserve and move the existing focused tests for the three
capabilities, including:

- Crypto RFC 8032 Ed448 behavior and SHA-256 coverage
- C and C++ public-header compile contracts
- filesystem service lifecycle, cancellation, capacity, callback, and watcher
  behavior
- filesystem publisher behavior
- process I/O lifecycle, cancellation, termination, and quiescent destruction

Tests must include the new `salts/...` public header paths and link the new public
target identities where target-level testing is appropriate.

No test may implement package verification by invoking nested CMake install +
consumer configure/build flows.

Repository CI may provide an already prepared matching Salts dependency prefix
to `salts-utils`; dependency selection still obeys the strict `SALTS_ROOT`
fail-fast rule.

## Source-repository cutover

The `qigao/salts` implementation change removes:

- `add_subdirectory(crypto)`
- `add_subdirectory(cflow-fs)`
- `add_subdirectory(cflow-process)`
- moved source/header/test/vendor ownership
- moved target entries from active build/export assumptions
- install-verification framework and fixtures
- active documentation claiming that Salts exports the moved targets

The low-level filesystem/process code and all shared CFlow/Core/Platform runtime
used by the destination remain untouched except where include or documentation
references must be corrected after ownership changes.

A repository-wide search must find no production/build references to
`Salts::CFlowFS` or `Salts::CFlowProcess` after cutover.

## Destination-repository cutover

The `qigao/salts-utils` implementation change adds the three migrated modules,
registers them in the normal root build, exports only `Salts::Crypto`,
`Salts::FS`, and `Salts::Process`, and updates architecture/README documentation
for the new ownership boundary.

A repository-wide search must find no public `include/cflow` directory for these
moved modules after cutover.

The destination must reject a pre-migration Salts dependency that still owns any
of the moved public targets instead of trying to coexist with it.

## Compatibility policy

This is intentionally a breaking package-boundary cleanup.

Preserved:

- C runtime behavior of the three capabilities
- crypto API and `salts/crypto.h`
- filesystem/process C function signatures and lifecycle contracts
- `Salts::Crypto` public target name

Changed intentionally:

- package owner: Salts -> SaltsUtils
- filesystem target: `Salts::CFlowFS` -> `Salts::FS`
- process target: `Salts::CFlowProcess` -> `Salts::Process`
- adapter headers: `cflow/...` -> `salts/...`

Not provided:

- target aliases
- forwarding headers
- package fallback
- duplicate target ownership
- source-tree fallback
- install-verification compatibility infrastructure

## Acceptance criteria

The migration is complete only when all of the following are true:

1. `qigao/salts` no longer builds, installs, or exports Crypto, CFlowFS, or
   CFlowProcess as moved capabilities.
2. `qigao/salts-utils` is the only owner of `Salts::Crypto`, `Salts::FS`, and
   `Salts::Process`.
3. No active public header for the migrated FS/Process adapters remains under
   `include/cflow`.
4. No `Salts::CFlowFS` or `Salts::CFlowProcess` compatibility alias exists.
5. Old/pre-migration Salts packages conflict explicitly and fail during
   `salts-utils` configuration.
6. No active CMake install-verification target, script, nested consumer fixture,
   preset, or CI step remains in either repository.
7. Existing capability behavior tests pass from the normal test graph.
8. Source scans for old target names, old public include paths, and active
   install-verification hooks are clean, excluding historical documentation
   where appropriate.
9. No runtime API behavior is changed solely to accomplish the ownership move.
