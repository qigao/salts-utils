# DataBind

Schema-driven C data binding built on Salts.

**Version:** 3.0.0  
**ABI:** 9  
**Tags:** C11 · CMake · Schema · Data Binding · CMeta · CSerde · TBE

DataBind owns schema and binding semantics. Native semantic type identity comes from Salts CMeta; container and serialization primitives come from Salts CSTL/CSerde. Concrete parser adapters and compiler helpers may use SaltsUtils explicitly, but the installed DataBind package remains a distinct owner.

## Provenance

DataBind originates in the `qigao/salts-utils` repository. The standalone repository root is a mechanical extraction of the DataBind/TBE-owned subtree from that source repository; it is not an independently originated implementation.

The extraction contract is tracked in `qigao/salts-utils#89`. The source repository remains the production owner until a dedicated imported repository is established, passes the same standalone gates, and downstream consumers are cut over. No forwarding package, duplicate production owner, compatibility alias, or source-tree fallback is part of that transition.

The validated extraction chain includes:

- `qigao/salts-utils` merge commit `887d490731f3e2340970a14ebb3c7f58d9fb73c2` (#97), which established the standalone DataBind/TBE project root;
- export branch `staging/databind-repository-root`;
- exact head `8cf2d7dda745d45fc1d0cd007c9a1dc61cfd8cc3`, which first passed the full repository-root configure/build/55-test/install-ownership gate.

## Architecture

```text
Salts
├── Core / CMeta / CSTL / CSerde
│
├── SaltsUtils
│   └── concrete parser adapters and compiler/build helpers
│
└── DataBind
    ├── schema model and TBE runtime
    ├── format-neutral DataBind core
    ├── JSON / YAML / CSV / XML / temporal adapters
    ├── CMeta / CFlow integration
    └── tbe_compiler + templates
```

The package boundary is intentionally strict:

- one canonical installed owner for DataBind targets;
- no forwarding package or compatibility alias;
- no source-tree fallback;
- no duplicate binder or semantic type system;
- no implicit parser fallback;
- SaltsUtils is not exported as the owner of DataBind runtime/package targets.

## Main CMake targets

- `Salts::TbeSchema`
- `Salts::DataBindCore`
- `Salts::DataBind`
- `Salts::DataBindCMeta`
- `Salts::DataBindCFlow`
- `Salts::DataBindJsonAdapter`
- `Salts::DataBindYamlAdapter`
- `Salts::DataBindCsvAdapter`
- `Salts::DataBindXmlAdapter`
- `Salts::DataBindTemporalAdapter`

Installed consumers use the package directly:

```cmake
find_package(DataBind 3 CONFIG REQUIRED)
target_link_libraries(app PRIVATE Salts::DataBind)
```

## Standalone build

DataBind requires CMake 3.27 or newer and an installed Salts profile. Building the concrete adapters and compiler also requires an installed SaltsUtils utility profile plus the explicit generator tools used by the repository.

The repository CI uses these inputs:

- `SALTS_ROOT`
- `SALTS_UTILS_ROOT`
- `VCPKG_ROOT`
- `DATABIND_RE2C_EXECUTABLE`
- `DATABIND_LEMPAR`
- `DATABIND_HOST_LEMON_EXECUTABLE`

A representative Linux debug build is:

```bash
cmake --preset linux-ci-debug -DBUILD_TESTING=ON
cmake --build build/linux-ci-debug --parallel
ctest --test-dir build/linux-ci-debug --no-tests=error --output-on-failure
```

Install the canonical package with:

```bash
cmake --install build/linux-ci-debug \
  --prefix /path/to/databind \
  --component DataBind
```

The installed root contains the DataBind CMake package, public headers, runtime libraries, `bin/tbe_compiler`, and compiler templates. It must not contain a SaltsUtils package owner.

## Repository layout

- `schema/` — schema model, parser and TBE schema runtime
- `data_bind/` — binding core, runtime and adapters
- `tbe_compiler/` — compiler and code-generation templates
- `cmake/` — DataBind-owned build/package helpers
- `tests/` — package and ownership contracts
- `vendor/` — private vendored dependencies

## Licensing

DataBind project code is distributed under the Apache License 2.0; see [LICENSE](LICENSE).

Vendored third-party code keeps its original license terms. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and the license headers in the corresponding vendored sources.
