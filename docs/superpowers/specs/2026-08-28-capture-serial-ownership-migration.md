# Capture And Serial Ownership Migration

## Background

`capture/` and `turbo_serial/` currently live in TurboUtils even though they are
device-facing subsystems rather than format-neutral foundation primitives. The
new repository boundary keeps TurboUtils as the one-way dependency and makes
TurboParser the package owner for these higher-level components.

## Decision

Move both complete source trees, including platform backends, tests, examples,
public headers, and package metadata, to TurboParser.

| Component | Old installed target | New installed target | Stable surface |
|---|---|---|---|
| Native capture | `TurboUtils::Capture` | `TurboParser::Capture` | `turbo_capture.h`, C symbols, enum values, layouts |
| Serial ports | `TurboUtils::turbo_serial` | `TurboParser::Serial` | `turbo_serial.h`, C symbols, enum values, opaque handles |

Do not add compatibility aliases in TurboUtils. A target has one package owner;
consumers migrate their `find_package` call and link target explicitly.

## Dependency And State Boundaries

The dependency direction remains one-way:

```text
consumer -> TurboParser::{Capture,Serial} -> TurboUtils::{Core,STL}
```

Capture keeps `TurboUtils::Core` private. Serial keeps `TurboUtils::Core` public
because its existing target contract exposes that dependency, while
`TurboUtils::STL` remains private. No TurboUtils source directory or vendor
directory is included from TurboParser.

No runtime data-path semantics change:

- capture callback frames remain borrowed views valid only until the callback
  returns; stop and destroy remain serialized control-plane operations;
- each serial handle owns its RX/TX storage;
- the RX ring remains SPSC from the async pump to one application reader;
- the TX ring remains SPSC from one application writer to the async pump;
- configured ring size remains a power of two with usable capacity `size - 1`;
- full/empty, wrong-mode, shutdown, and I/O failures retain their existing
  `turbo_serial_result_t` results.

## Build And Packaging

TurboParser gains `TURBO_ENABLE_CAPTURE`, the vcpkg `capture` feature, and the
existing Windows/Linux capture presets. Android TurboParser profiles enable the
feature exactly as the old TurboUtils Android profiles did. Capture exports only
when enabled; Serial is part of the default package.

TurboUtils removes the two subdirectories, capture option and feature, capture
presets, installed-consumer branch, and export verification dependencies.
Historical dated design documents remain as records of the previous ownership;
current architecture and module README files describe the new owner.

## Alternatives

1. Keep both components in TurboUtils. Rejected because it keeps device/media
   subsystems in the foundation package and conflicts with the requested owner.
2. Create a new device or media repository. Deferred because it adds a third
   package, release train, and dependency root without being requested.
3. Export aliases from both packages. Rejected because duplicate ownership
   makes package resolution and eventual removal ambiguous.

## Migration, Compatibility, And Rollback

Merge TurboParser ownership first, migrate downstream consumers to the new
targets, then merge TurboUtils removal. During that interval both packages can
provide the same C header and binary names from separate install prefixes, but a
consumer must link only one owner.

Rollback is repository-local: revert TurboUtils removal while retaining the
TurboParser addition, or revert the TurboParser addition before TurboUtils
removal merges. No data migration or file-format conversion is involved.

## Verification

- focused capture and serial unit/ABI tests;
- TurboParser default and capture-enabled configure/build/test/install presets;
- external installed consumers for `TurboParser::Capture` and
  `TurboParser::Serial`;
- installed export audit for source paths, private targets, and obsolete owner
  names;
- TurboUtils full configure/build/test/install after removal;
- Android capture configure/build when the local NDK environment is available.
