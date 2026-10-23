# DataBind schema-text API and runtime-linkage inventory

Issue: [#588 — remove production runtime dependency on IDL frontend and Binary compiler](https://github.com/qigao/salts-utils/issues/588)

Audit base: SaltsUtils master `bee87376cafd813e298a86e4b2c6f7be7416d4f0` (2026-10-23).

This document records the current public surface and observed source consumers before changing target boundaries. It is an inventory, **not a claim that the dependency has been removed**.

## 1. Public runtime API that compiles schema text

`databind/runtime/data_bind.h` exposes:

- `data_bind_create(const char *schema_path, DataBind **out_codec, DataBindError *error)` (declaration around line 589): reads a schema file and compiles it at runtime.
- `data_bind_create_from_text(const char *schema_text, size_t len, DataBind **out_codec, DataBindError *error)` (declaration around line 606): compiles caller-supplied schema text at runtime.
- `data_bind_free(DataBind *codec)` destroys the resulting codec.

The API documentation currently promises that codec creation parses schema directly in the C runtime, without `salts-idlc`, generated headers, or generated source. These are public APIs, not compiler-private helpers. Removing them from the runtime link closure without a compatibility/version plan would break existing consumers.

## 2. Production implementation dependency path

`databind/runtime/CMakeLists.txt` currently links the production `data_bind` target privately to `Salts::Schema`, `Salts::IDL`, and `databind_binary_contract`.

`databind/runtime/data_bind.c` directly includes `node_tree.h`, `schema_cmeta.h`, `idl.h`, `binary_contract_overlay.h`, and `binary_layout_lowering.h`. The schema creation path:

1. Reads source text (`data_bind_create`) or accepts it in memory (`data_bind_create_from_text`).
2. Builds the legacy `Node` tree through `idl_parse` and applies `databind_binary_contract_apply`.
3. Builds a typed `IdlContract` through `idl_contract_parse`.
4. Stores both representations in `DataBind`.
5. Frees both the tree and typed contract in `data_bind_free`.

The rest of `data_bind.c` is not merely a parser wrapper: JSON/XML/CSV binding and default handling traverse `Node`; Binary decoding uses `IdlField`/`IdlDataDecl`; Binary execution plans use the selected format/layout APIs. Therefore deleting CMake link lines or moving only the two public functions cannot complete #588. The runtime needs a normalized compiled metadata representation before the parser/tree can be detached.

## 3. In-repository references found

The source searches for both `data_bind_create` and `data_bind_create_from_text` found these in-repository call sites. They are categorized to make migration order explicit.

### Generated/public source path

- `databind/compiler/templates/c/c_typed_source.mustache`: generated `*_codec_create()` embeds schema text and calls `data_bind_create_from_text`. This is the primary generated-code contract to replace with compiled/embedded metadata; otherwise every generated consumer still asks runtime to parse IDL.
- `databind/compiler/package_config/databind_public/main.c`: installed public API smoke.
- `cmake/ci/probe_monocypher_odr.c`: runtime symbol/DSO admission probe; it checks that the public API symbol is present.

### Runtime benchmarks and tests

- `databind/runtime/benchmark_data_bind_native.c`
- `databind/runtime/benchmark_data_bind_json_path.c`
- `databind/runtime/benchmark_data_bind_pool.c`
- `databind/runtime/test_data_bind.c`
- `databind/runtime/test_data_bind_pool.c`
- `databind/runtime/test_data_bind_public_api.c`
- `databind/runtime/test_data_bind_record.c`
- `databind/runtime/test_data_bind_record_exact.c`
- `databind/runtime/test_data_bind_channel.c`
- `databind/runtime/test_data_bind_component.c`
- `databind/runtime/test_data_bind_cflow.c`
- `databind/runtime/test_data_bind_validation_plan.c`
- `databind/runtime/test_data_bind_cmeta.c`
- `databind/runtime/test_data_bind_cmeta_reflection.c`
- `databind/runtime/test_data_bind_dynamic_cstl.c`
- `databind/runtime/test_data_bind_value_boundary.c`
- `databind/runtime/test_tbe_cmeta_acceptance.c`
- `databind/runtime/test_data_bind_projection_plan.c`
- `databind/runtime/test_data_bind_object_message_plan.c`
- `databind/runtime/test_generated_message_plan.c`
- `databind/runtime/test_data_bind_native_nullable.c`
- `databind/runtime/test_data_bind_binary_native.c`
- `databind/runtime/test_data_bind_socket_execution_plan.c`
- `databind/compiler/tests/multi-transport/test_public_opaque_service.c`
- `databind/compiler/tests/multi-transport/test_public_binary_transport_provider.c`
- `databind/compiler/tests/binary/test_binary_layout_ir.c`
- `databind/tests/native_storage/native_value_reader_test.c`
- `databind/tests/native_storage/native_collection_message_test.c`
- `databind/tests/native_storage/native_bytes_json_test.c`
- `databind/tests/native_storage/native_value_reader_cpp_test.cpp`

These tests should migrate to a compiled-contract fixture or an explicitly linked schema-tooling fixture according to what they are testing. Parser-validation tests must remain parser tests; runtime binding tests should not silently compile the IDL under test.

## 4. Cross-repository consumers found

A source search across current downstream repositories found these references:

| Repository | Source paths | Required migration decision |
| --- | --- | --- |
| [TurboAgent](https://github.com/qigao/TurboAgent) | `agent_compiler/src/turbo_agent_compiler.c`; `agent/tests/rpc_generated/test_turboagent_typed_rpc.c` | Compiler/factory call must be explicit tooling; generated RPC runtime should consume a compiled contract. |
| [TurboScript](https://github.com/qigao/TurboScript) | `turbo_script/src/databind/turbo_script_class_databind.c` | Class mapping must consume canonical compiled schema/contract data or explicitly opt into the tooling adapter. |
| [TurboDB](https://github.com/qigao/TurboDB) | `orm/tests/flow/orm_row_publisher_test.c`; `orm/tests/flow/sqlite/orm_public_flow_test.c`; `orm/tests/e2e/cross_driver/sql_validation_parity_test.c` | Test-only dynamic schema compilation can use the tooling adapter; preserve validation parity. |
| [CHttp](https://github.com/qigao/CHttp) | `openapi/tests/test_databind_http_provider.c`; `app/tests/web/test_chttp_web_form_plan.c`; `app/tests/service/chttp_service_http_test.c`; `http_server/tests/rpc_generated/chttp_rpc_service_test.c`; `http_server/tests/rpc_generated/chttp_rpc_owned_native_test.c`; `app/tests/web/chttp_web_form_plan_contract_test.cmake` | Test/factory entrypoints must be migrated; generated production RPC artifacts must not acquire a parser fallback. |

The list above is based on exact symbol search, not an assumption that all other consumers are absent. Re-run the repository-wide search after each migration slice and include any new consumers discovered.

## 5. Recommended cutover sequence

1. Add a public immutable compiled-schema/contract import API whose storage and identity are owned by a versioned DataBind schema artifact. It must not accept arbitrary `Node *` or expose compiler-private structures.
2. Move text/file parsing into an explicit `Salts::IDLTooling` / schema-factory target. Make the target boundary visible in CMake and package exports; do not hide it behind runtime fallback.
3. Update `c_typed_source.mustache` and installed consumer fixtures to construct runtime codecs from compiled metadata rather than embedded IDL text.
4. Migrate production consumers (TurboAgent compiler/generated RPC, TurboScript mapper) and test-only dynamic factories (CHttp/TurboDB) separately.
5. Split runtime metadata traversal from the legacy `Node` tree, then remove `Salts::IDL`, `node_tree` parser linkage, and `databind_binary_contract` from the production runtime target. Keep Binary readers/writers and immutable Binary execution plans where explicitly selected.
6. Verify exported symbols and ELF/PE/Mach-O dependencies, installed C11/C++17 consumers, schema diagnostics, format fingerprints, and JSON/YAML/XML/CSV/Binary/MethodPlan/Plugin/CFlow regression tests.

## 6. Explicit non-goals

- Do not remove Binary as a supported format.
- Do not silently fall back from compiled metadata to parsing schema text at runtime.
- Do not export the internal `Node` tree as a new public ABI.
- Do not break the existing `data_bind_create*` ABI in the 4.3 line without a reviewed compatibility/version decision.
- Do not claim #588 complete until the production runtime target's actual link/symbol closure proves the parser/compiler dependency is gone.
