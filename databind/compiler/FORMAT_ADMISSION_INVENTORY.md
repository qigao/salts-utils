# DataBind IDL compiler — current C/Native and Binary admission inventory

Status: reconciled against master `1b3ad84953926ba397214551a9950ab290022e77` (2026-10-23). Tracks [#599](https://github.com/qigao/salts-utils/issues/599).

This is a current-state inventory, not a claim that every C/native path has completed the format cutover. Contract-only NativeSourceIR and rollback-aware output staging are delivered; legacy Binary-mutated Node consumers and the exhaustive field-by-field audit remain.

## Authority and boundaries

```text
IDL source
  -> idl_parse(Node) -> immutable IdlContract
       |-> Contract-only language/source projection
       |-> Contract-only NativeSourceIR for admitted C/native shapes
       |-> explicitly selected BinaryFormatPlan -> BinaryLayoutIR -> CSerde
       |-> selected artifact/transport plan
```

The logical `IdlContract` is the semantic source of truth. Binary offsets, alignment, fixed-block layout, byte order, presence/null bitmaps and wire fingerprints belong only to a selected Binary representation. Legacy C templates still consume Binary-overlay facts; they must not be treated as the NativeSourceIR representation.

## Observed responsibilities

| Location | Current responsibility | Remaining risk / admission rule |
| --- | --- | --- |
| `compiler_core.c:databind_compiler_parse_contract_file_mode` | Uses explicit contract-only vs Binary admission modes; freezes the typed Contract before applying the legacy Binary overlay when that path is selected. | The mode split exists, but some legacy C/Native consumers still read overlay-mutated `Node` fields. |
| `native_source_ir.c` | Contract-only C header / typed native representation for the admitted NativeSourceIR subset; canonical C record names and owning lifecycle helpers. | Does not yet cover every optional/nullable/nested/owning execution shape. Unsupported layouts must fail closed. |
| `compiler/binary/src/binary_contract_overlay.c` | Legacy Binary layout facts such as offsets, field sizes, fixed block sizes and presence/null wire representation. | Retain only for selected Binary/legacy companion paths; do not use as native semantic authority. |
| `templates/c/c_structs.mustache` | Legacy Binary-coupled C declarations and wire helpers. | It is not the bare-C source-only renderer. Remove remaining unconditional consumers only after native parity is proven. |
| `templates/c/c_typed_source.mustache` | Legacy typed source / codec companion and Binary reader/writer behavior. | Its runtime schema-text factory call is a separate #588 linkage issue; do not claim the runtime parser is detached by #599. |
| `binary_reader_codegen.c` and Binary layout IR | Build readers/writers from a selected Binary format/layout plan. | Keep field ordering, offsets, alignment, byte order and wire fingerprint in the selected plan. |
| `compiler_core.c:tbe_compiler_run_owned` and `projection.c` | Validates the selected request/backend registry and the declared output manifest before publication. | Future backends must declare all outputs and a staging policy; never infer capability from file names. |

## Current admission map

| Request | Current behavior | Status |
| --- | --- | --- |
| Bare C, no source/guest companion and no selected Binary transport | Contract-only NativeSourceIR; does not use the legacy Binary header template. | Delivered for the admitted NativeSourceIR subset. |
| `ARTIFACTS TYPES` | Contract-only native type declarations. | Delivered. |
| `ARTIFACTS NATIVE` without Binary codec/transport | Contract-only Native Service metadata/executable adapter for admitted shapes. | Delivered for the qualified slice; expand owning/stateful shapes separately. |
| Plugin-only Function | Contract-only type header and Plugin export for admitted shapes. | Delivered for the qualified slice. |
| Contract-only Wasm | Narrow Core memory32 `list<u8>` envelope; unsupported layouts reject before publication. | Partial; expand only with explicit admission tests. |
| C with legacy `--source-output` / `--guest-output` companion | Selects the legacy Binary codec/bridge path and its generated helpers. | Still Binary-coupled; do not silently reinterpret this CLI contract. |
| Explicit `BINARY_CODEC` or a transport whose wire ABI still requires Binary | Selected Binary format/layout plan and its CSerde/transport consumers. | Keep supported; all selected-format admission must happen before output publication. |
| SQL, TypeScript, Python, Go, Rust and C++ source-only projection | Typed Contract plus backend presentation metadata; no implicit Binary layout. | Delivered for their admitted source shapes. |

## Delivered implementation milestones

- [x] First evidence-backed per-backend inventory and format-axis witness: a Binary-incompatible logical schema can pass source-only artifact rendering while the same schema fails when a Binary transport is explicitly selected.
- [x] Registry preflight before source publication: invalid/duplicate/missing backend requests fail before caller output changes.
- [x] Built-in output staging and rollback for named header/source/guest/DSL paths.
- [x] Bounded complete output manifest including Native Service, Plugin, Wasm, OpenAPI and transport outputs.
- [x] [PR #606](https://github.com/qigao/salts-utils/pull/606): declared `STAGED_MULTI` for multi-file Native/Plugin/Wasm generation, reserves every manifest output before publishing, and rejects duplicate paths or invalid staging claims. Selected stage-safe single-output backends use the same rollback-aware coordinator.
- [x] Replace the former shared `require_binary_format` Boolean with an explicit format-admission mode/enum. This does **not** remove every legacy overlay consumer.

These milestones provide failure-atomic publication for the declared named-output transaction. They do not promise power-failure durability, a simultaneous multi-path snapshot to concurrent readers, or journaled crash recovery. Stdout output also remains outside the named-file transaction.

## Remaining implementation slices

- [ ] Finish the exhaustive field-level inventory of every Binary-overlay-derived value consumed by C templates, typed source, reader codegen, guest and Plugin/Wasm/Socket/FlowMQ plans.
- [ ] Remove remaining legacy Binary-mutated `Node` semantic consumers from C/native lowering, one artifact family at a time.
- [ ] Expand NativeSourceIR to supported owning/optional/nullable/nested shapes from canonical CMeta/CSTL descriptors without deriving lifecycle semantics from wire offsets.
- [ ] Complete the C-only, C+source, C+guest, C+Socket/FlowMQ and CMeta/native consumer matrix across Linux/Windows/macOS/Android/iOS, installed SDK and unsuppressed sanitizers.
- [ ] Add callback-failure-after-stage-write tests for each remaining backend and ensure all old outputs are preserved; unknown or incomplete output capabilities must fail before publication.

## Regression acceptance

| Fixture | Required behavior |
| --- | --- |
| Binary-incompatible source Contract | Contract-only NativeSourceIR output succeeds for supported logical types. |
| Same Contract + explicit Binary codec/transport | Rejects before publishing any caller output. |
| Mixed Native + Plugin + Wasm + OpenAPI/transport selection | All declared paths are preflighted and staged before commit; no partial final output on generator failure. |
| Duplicate path, case/slash alias or dishonest staging policy | Fails before publication, including Windows path aliases. |
| Generator writes a partial staged file then fails | Original files remain byte-for-byte unchanged; previously absent outputs remain absent. |
| Installed C11/C++17 consumer | Generated headers/sources compile and exercise the selected native/codec contracts on supported platforms. |

## Related issues

- [#578](https://github.com/qigao/salts-utils/issues/578): canonical NativeSourceIR/CMeta lifecycle lowering.
- [#588](https://github.com/qigao/salts-utils/issues/588): production DataBind runtime/parser/Binary compiler linkage separation.
- [#595](https://github.com/qigao/salts-utils/issues/595): source-language cutovers.
