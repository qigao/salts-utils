# DataBind IDL compiler — C/Native and Binary admission inventory

Status: first evidence-backed cutover plan for [#599](https://github.com/qigao/salts-utils/issues/599). A completed TypeScript/Python/Go/Rust/C++ or SQL **source** cutover does not imply that native C or binary codec output is format-neutral.

## Authority and boundaries

```text
IDL source
  -> idl_parse(Node) -> idl_contract_build_from_tree() -> immutable IdlContract
     |-> Contract-only SQL / TS / Python / Go / Rust / C++ presentation
     |-> C/native typed declarations + CMeta/CSTL [not yet independent]
     |-> explicitly selected Binary FormatPlan -> BinaryLayoutIR -> CSerde
     |-> selected transport/artifact generator
```

### Observed responsibilities (current source)

| Location | Consumed/produced facts | Admission consequence |
| --- | --- | --- |
| `compiler_core.c:databind_compiler_parse_contract_file_mode` | `IdlContract` freezes before `databind_binary_contract_apply(root)` and `tbe_compiler_annotate_language_types(contract, root)`. | Shared legacy C parse remains Binary-admitted; source languages and SQL bypass overlay. |
| `compiler/binary/src/binary_contract_overlay.c` | `offset`, `has_offset`, `field_size_bytes`, `fixed_block_size`, `is_fixed_size`, and layout validation; updates presence/null bitmap layout. | Wire layout authority. Cannot be replaced by arbitrary source representation metadata. |
| `templates/c/c_structs.mustache` | Imports `data_bind_binary_wire.h`, emits wire byte order, fixed block length, `*_OFFSET`, byte reads/writes, group cursor helpers; also native declarations and CMeta symbols. | Mixing wire accessors with native declarations currently makes bare `--lang c` Binary-dependent. |
| `templates/c/c_typed_source.mustache` | CMeta/CSTL record descriptors, optional/null bits, typed native field metadata, `binary_reader_supported`, canonical native-to-Binary writer and unavailable-Binary gates. | Native lifetime support cannot be inferred from whether Binary was admitted. Requires separate C/native projection plan. |
| `compiler_core.c:tbe_compiler_annotate_binary_reader_messages` and `binary_reader_codegen.c` | Contract + `databind_binary_format_plan` + layout graph and CSerde representation. | Explicit Binary-format generation, not generic source lowering. |
| `compiler_core.c:tbe_compiler_run_owned` | Prepares `projection_root` for selected projection or `--source-output`; uses `tbe_compiler_projection_requires_binary`, then renders primary source, source/guest/DSL, finally dispatches backend generators. | Selection/format validation and output publication are not fully transactional today. |
| `compiler/projection.c:databind_compiler_projection_run` | Validates typed request IDs, backend names/callback registry; invokes selected backends in request order. | Registry admission originally occurred after main file rendering. First #599 slice moves **registration preflight** before rendering; backend callback failures remain distinct. |

### Admission map — existing vs target

| Requested output | Existing admission | Planned owner |
| --- | --- | --- |
| SQLite/PostgreSQL | Logical Contract -> dialect IR; no implicit Binary | Database plan |
| TS / Python / Go / Rust / C++ | Logical Contract -> backend presentation IR; no implicit Binary | Language source plan |
| C header without companion | Binary overlay + CMeta/native annotations + Binary helpers | Split Native/CMeta source projection from optional wire artifact; **not yet done** |
| C header + `--source-output` | Binary + native CMeta/CSTL + CSerde/reader plan | Native plan **and** explicitly selected Binary plan |
| C + `--guest-output` | Binary-coupled header plus bridge adapter | Explicit bridge format and C/native prerequisites |
| Typed artifact-only projection | Contract; backend may use configured native or format contract | Selected artifact's own declared prerequisites |
| Transport (HTTP/RPC/Socket/FlowMQ/MQTT/WebSocket) | Compiler currently requests Binary plan for transport-axis selections | Enumerate actual required format per transport before changing policy |

## First tested improvement in #599

Before any render or generator callback, `databind_compiler_projection_selection_valid()` now checks the **complete** selected ID/backend registry: valid IDs, uniqueness on each axis, canonical backend names, callable backend functions, and a registered backend for each selected request. `tbe_compiler_run_owned()` invokes this test **before parsing or publishing any source output**.

Regression checks include:
1. Missing backend, duplicate request, duplicate backend or misspelled backend rejects before replacing an existing C header, creating the C companion source or invoking any callback.
2. Same protection for a TS source output with a registered backend name mismatch.
3. A valid source-only **artifact** selection admits a logical Contract with `string` before `uint32`, despite Binary layout ordering restrictions.
4. The **same** source Contract with an explicitly selected Binary transport fails before touching the TS output or invoking its transport callback.

This is **not** a multi-output transaction. Once a valid backend begins executing, its generation callback may still fail *after* the main source was published. No claim of global atomicity should be made until the following slice adds transaction-scoped staging and commit.

## Selected projection file manifest (follow-up after #601)

The public projection frontend now publishes a **complete current output-path
manifest** for built-in C outputs plus every selected artifact/transport
generator. Each item records its pathname and the typed projection identity
that owns it; compiler-owned header/source/guest/DSL paths use an empty owner.
The manifest is bounded (`DATABIND_COMPILER_FRONTEND_MAX_OUTPUTS`), and
missing paths, duplicate names, and collisions involving secondary
Plugin/Native Service/Wasm output paths fail before generation.

```text
Compiler planned paths
  built-in: header, source, guest, DSL
  artifacts:
    Native Service: .service_native.c + .service_native.h
    Plugin: .plugin.c + .plugin.h + .plugin_client.h + .plugin_client.c
    Wasm: .wasm + .wasm.h + .wasm.c + .wasm_guest.h
    OpenAPI: .openapi.json
  transports:
    HTTP/RPC/Socket/FlowMQ: selected Method/Socket/FlowMQ output
```

For the moment it is a **read-only planning and collision-validation
contract**. Output paths borrow memory from their frontend input or
compiler-owned frontend plan; consumers must not retain them after that
plan is disposed. This inventory does **not** change the legacy
`databind_compiler_projection_generate_fn` callback contract, does not
retroactively stage backend-generated files, and does not roll back a prior
backend callback if a later one fails. Cross-backend transaction staging
must consume this manifest through a separate explicit prepare/commit
protocol, rather than guessing which filenames a generator touched.

## Remaining implementation slices

- [ ] Isolate a compiler-private **NativeSourceIR** built from typed Contract + canonical CMeta/CSTL providers without wire byte offsets.
- [ ] Give bare C source-only, native-binding, C+codec and guest outputs explicit admission contracts; remove Binary helpers from an actual source-only header rather than providing a second compatibility template.
- [ ] Make selected Binary consumers share one `BinaryFormatPlan` and perform all selected-format preflight before any publication.
- [ ] Introduce temporary staging/transaction commit for all requested output paths and generator callbacks; handle rollback/cleanup errors visibly, not in no-fail destructors.
- [ ] Remove shared `require_binary_format` Boolean and legacy Binary-mutated Node semantic consumers after full Native/CSerde/transport qualification.
- [ ] Run C-only, C+source, C+guest, C+Socket/FlowMQ, SDK-installed consumer builds across Windows/Android/macOS/Linux + Sanitizers; assert native CMeta/RAII identity.

Related: [#578](https://github.com/qigao/salts-utils/issues/578) native CMeta convergence, [#588](https://github.com/qigao/salts-utils/issues/588) runtime/frontend linkage separation, [#595](https://github.com/qigao/salts-utils/issues/595) source language cutovers.
