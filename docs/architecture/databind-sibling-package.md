# DataBind sibling package architecture

Status: active migration tracked by [salts-utils #67](https://github.com/qigao/salts-utils/issues/67)

## Goal

DataBind is a sibling of salts-utils and salts-net, not a sub-layer that requires
the general SaltsUtils runtime bundle.

```text
                 Salts
               /   |   \
      salts-utils  |  DataBind
                   |
               salts-net
```

The physical repository move comes after the package and ABI boundary is real.

## Canonical ownership

### Salts

Salts owns native semantic/runtime foundations:

- CMeta semantic type identity and data descriptors;
- CSTL storage primitives;
- CSerde format-neutral token contracts;
- Core/platform primitives;
- CFlow only for the optional DataBind CFlow adapter.

### DataBind

DataBind owns:

- schema overlay, validation and fingerprints;
- schema model/compiler contracts;
- native and dynamic value binding;
- transactional conversion and rollback;
- generated typed bindings;
- format-neutral stream/binding orchestration;
- DataBind-owned public diagnostics and value representations;
- DataBindCMeta and DataBindCFlow adapters.

CMeta and DataBind do not form competing type systems. CMeta owns native type
identity. DataBind owns external schema/binding semantics.

### salts-utils

salts-utils owns concrete utility implementations such as QueryVM, JSON/YAML/CSV/XML
parsers, DateTimeParser, Mustache and CmdParser.

Those utilities may implement DataBind adapters or build-time compiler tooling,
but they must not become public runtime ABI types of the DataBind core.

## DataBind 3.0 / ABI 9 cut

The first migration slice makes the public header implementation-neutral:

- `DataBindDateTime` is DataBind-owned rather than an alias of `datetime_t`;
- `DataBindQueryStatus` is DataBind-owned rather than `qvm_status_t`;
- `DataBindQueryDiagnostic` no longer exposes QueryVM types;
- `data_bind.h` does not include QueryVM or DateTime parser headers;
- QueryVM/DateTime parser targets are private implementation dependencies.

This is intentionally a major ABI boundary, not a compatibility shim.

## Next runtime cut

The current runtime still directly implements JSON/YAML/CSV/XML and path-query
orchestration using SaltsUtils-owned parser targets. The next slice must move
those mechanics behind an explicit format adapter / CSerde-facing contract so
the core runtime can build from Salts alone.

No runtime plugin discovery or format fallback is required. Provider selection
must remain explicit and bounded.

## Package cut

Before a physical repository move, this repository will produce an independent:

```cmake
find_package(DataBind CONFIG REQUIRED)
```

package/export set.

After the cut:

- SaltsUtils no longer exports DataBind/TBE targets;
- each target has exactly one owner;
- a consumer must resolve the explicit DataBind package root;
- stale SaltsUtils-owned DataBind exports fail fast;
- there is no duplicate export, forwarding package, source-tree fallback or
  compatibility alias.

## Compiler

The compiler is a build-time tool. It may consume optional SaltsUtils tooling
such as Mustache/CmdParser without adding SaltsUtils to the deployed DataBind
runtime closure.

## Consumer migration

RulesForge, TurboFlow and other consumers migrate only after the independent
package boundary is installable and validated. The physical repository extraction
is the final step, not the mechanism used to create the boundary.
