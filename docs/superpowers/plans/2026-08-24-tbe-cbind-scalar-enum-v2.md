# TbeCBind v2 Fixed Scalar and Enum Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 runtime TbeCBind 与 generated CBind sidecar 以同一能力矩阵支持 bool、完整固定宽度整数、UUID 和普通 enum，同时保持 DataBind-free、preflight fail-fast 与事务回滚。

**Architecture:** 先在 TurboUtils 通用 CBind 层补齐 descriptor-driven fixed-width integer decode 和 `turbo_uuid_t` CMeta adapter；再在 TurboParser TbeSchema-owned internal module 建立唯一 scalar capability matrix，由 runtime schema/native plan 与 tbe_compiler sidecar 共同消费。Enum 执行复用 TurboUtils 已合并的 enum ops/decode，TurboParser 只负责编译并校验 schema/native metadata。

**Tech Stack:** C11, C17/C++17 consumer checks, CMake Presets, TurboUtils CMeta/CSerde/CBind/Core, TurboParser TbeSchema/TbeCBind/tbe_compiler/JSON CSerde reader, Mustache, TinyTest.

**Spec:** `docs/superpowers/specs/2026-08-24-tbe-cbind-scalar-enum-v2.md`

## Global Constraints

- 不修改任何 `vendor/` 内容，也不触碰原始 TurboUtils 工作树中的用户改动。
- 每项行为先写失败测试，确认失败原因与缺失能力一致，再写最小实现。
- runtime 与 generated sidecar 不得各自维护 scalar type 字符串列表。
- 不新增 DataBind include/link/call/fallback；不引入动态值树、JIT 或运行时代码生成。
- 普通 enum 本阶段支持；flags、optional/default/alias/container/group collection/union 继续稳定 fail fast。
- 所有 descriptor 在读取输入前验证 kind、bits、size、alignment、offset、ops 和 semantic metadata。
- UUID 输入只接受 canonical 36-byte string；destination 全零是唯一 semantic-zero 状态。
- 只使用仓库既有 CMake/TinyTest/format 方式；`.codegraph/` 与 SDD ledger/report 不提交。

---

## Task 1: TurboUtils prerequisite — fixed-width integers and UUID metadata

**Repository/worktree:** 从 `qigao/turbo-utils` 最新 `origin/master` 创建独立 linked worktree；不得在带有 `cbind/CMakeLists.txt` 用户改动的原始工作树开发。

**Files:**

- Modify: `cbind/src/scalar.c`
- Modify: `cbind/tests/cbind_scalar_decode_test.c`
- Modify: `utils/include/turbo_cmeta_data.h`
- Modify: `utils/tests/test_turbo_cmeta_data.c`
- Modify: `utils/tests/test_turbo_cmeta_data_cpp.cpp`
- Modify only if required by existing test registration: `cbind/CMakeLists.txt`, `utils/CMakeLists.txt`
- Add: `docs/superpowers/specs/2026-08-24-cbind-fixed-width-uuid-storage.md`
- Add: `docs/superpowers/plans/2026-08-24-cbind-fixed-width-uuid-storage.md`

- [ ] Add scalar decode tests with custom valid descriptors for signed/unsigned 8/16/32/64 storage. Cover min/max, cross-signed token acceptance, negative-to-unsigned rejection, one-past-range, integral float, fractional/nonfinite rejection, wrong bits/size/alignment, unchanged destination on failure, and legacy `int/long/size_t` behavior.
- [ ] Run the focused CBind scalar target and record the RED failure caused by current canonical-type-only validation/decode.
- [ ] Generalize integer preflight to require `CHAR_BIT == 8`, bits in `{8,16,32,64}`, exact `storage_type->size`, and descriptor validity without requiring storage identity to be `int/long/size_t`.
- [ ] Decode through exact `intN_t/uintN_t` temporaries plus `memcpy`; retain current strict float conversion and `CBIND_VALUE_OUT_OF_RANGE` semantics. Do not weaken struct field storage identity checks.
- [ ] Add header-local stable CMeta type/data descriptors for all fixed-width integer types in `turbo_cmeta_data.h`; assert size assumptions at compile time.
- [ ] Add UUID tests first: fixed 16-byte storage, lowercase/uppercase canonical input, exact length/hyphens/hex validation, no NUL requirement, max-buffer enforcement, occupied destination handling, restore-zero idempotence, and C++ header compile.
- [ ] Implement `turbo_uuid_t` CMeta type, owned STRING buffer ops and data descriptor using bounded non-allocating parsing. Ensure assign never observes bytes past the provided slice and restores all-zero on failure.
- [ ] Run focused CMeta/Core/CBind tests, full TurboUtils Release CTest, installed-package consumer verification, dependency closure and `git diff --check`.
- [ ] Commit the prerequisite branch, push it, and create a TurboUtils PR that states TurboParser issue #5 as consumer. Do not merge automatically.

Expected focused commands use the TurboUtils versioned user preset and exact target names discovered from its CMake files, followed by:

```powershell
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user
ctest --preset win-release-user --output-on-failure
cmake --build --preset win-release-user --target verify_installed_package
```

## Task 2: Shared TurboParser scalar capability matrix

**Files:**

- Add: `tbe/schema/src/tbe_cbind_capability.h`
- Add: `tbe/schema/src/tbe_cbind_capability.c`
- Add: `tbe/schema/test/test_tbe_cbind_capability.c`
- Modify: `tbe/schema/CMakeLists.txt`
- Modify: `tbe/tbe_cbind/CMakeLists.txt`
- Modify: `tbe/tbe_compiler/CMakeLists.txt`

- [ ] Write a focused table-driven TinyTest that queries every canonical spelling and alias, checks bool/signedness/bits/C storage/metadata symbols/UUID classification, and rejects unknown types, bytes and named records.
- [ ] Add a compile/link test proving both TbeCBind and tbe_compiler consume the same capability provider rather than copied tables.
- [ ] Run the new focused target and record RED because the provider does not exist.
- [ ] Implement a private immutable capability table and minimal lookup API owned by TbeSchema internals. Keep it uninstalled and out of public TurboParser headers.
- [ ] Compile the provider into the existing TbeSchema target and give both consumers only a build-tree PRIVATE include path. Do not add a target, installed header, or public/transitive dependency.
- [ ] Delete the duplicated `tbe_cbind_scalar_kind` string chain and `tbe_compiler_cbind_scalar_supported` list only when their callers use the shared provider.
- [ ] Run schema, TbeCBind schema and tbe_compiler focused tests; verify existing v1 spellings remain identical.
- [ ] Commit Task 2 independently.

## Task 3: Runtime TbeCBind fixed scalar and UUID support

**Files:**

- Modify: `tbe/tbe_cbind/src/tbe_cbind_internal.h`
- Modify: `tbe/tbe_cbind/src/schema_model.c`
- Modify: `tbe/tbe_cbind/src/native_shape.c`
- Modify: `tbe/tbe_cbind/src/plan_builder.c` only if semantic kind materialization requires it
- Modify: `tbe/tbe_cbind/test/tbe_cbind_test_fixtures.h`
- Modify: `tbe/tbe_cbind/test/tbe_cbind_multitu_fixture.c`
- Modify: `tbe/tbe_cbind/test/test_tbe_cbind_schema.c`
- Modify: `tbe/tbe_cbind/test/test_tbe_cbind_plan.c`
- Modify: `tbe/tbe_cbind/test/test_tbe_cbind_decode.c`
- Modify: `tbe/tbe_cbind/test/test_tbe_cbind_json.c`

- [ ] Add native fixtures containing bool, every fixed-width signed/unsigned type and `turbo_uuid_t`, with complete reflected layout/data descriptors from the new TurboUtils metadata.
- [ ] Add schema acceptance tests for canonical names and aliases, plus native mismatch cases for kind, signedness, bits, size, alignment and UUID-vs-string storage. Assert exact phase/path/status and `plan == NULL`.
- [ ] Add token and JSON decode tests for scalar min/max, out-of-range rollback, UUID lowercase/uppercase input, invalid UUID rollback, nested record and renamed semantic/native members.
- [ ] Run TbeCBind schema/plan/decode/JSON targets and record RED because the model still rejects these types.
- [ ] Extend semantic field representation with capability identity instead of one enum constant per spelling. Native preflight must compare kind/bits/size/alignment and UUID adapter identity/ops before plan publication.
- [ ] Reuse native scalar/UUID descriptors in the immutable overlay; do not add decode callbacks or DataBind adapters to TbeCBind.
- [ ] Keep optional/default/alias/bytes/container/group/union unsupported tests intact; split bool/width/UUID out of the old rejection table.
- [ ] Run all focused TbeCBind tests, multitu tests, path-safety/install-consumer tests and dependency-negative checks.
- [ ] Commit Task 3 independently.

## Task 4: Runtime ordinary enum schema/native validation

**Files:**

- Modify: `tbe/tbe_cbind/src/tbe_cbind_internal.h`
- Modify: `tbe/tbe_cbind/src/schema_model.c`
- Modify: `tbe/tbe_cbind/src/native_shape.c`
- Modify: `tbe/tbe_cbind/src/plan_builder.c`
- Modify: `tbe/tbe_cbind/test/tbe_cbind_test_fixtures.h`
- Modify: `tbe/tbe_cbind/test/test_tbe_cbind_schema.c`
- Modify: `tbe/tbe_cbind/test/test_tbe_cbind_plan.c`
- Modify: `tbe/tbe_cbind/test/test_tbe_cbind_decode.c`
- Modify: `tbe/tbe_cbind/test/test_tbe_cbind_json.c`

- [ ] Add schema tests for default and explicit enum underlying widths, exact/implicit values, duplicate symbol/value, out-of-range value, type-name collision and explicit flags rejection.
- [ ] Add native enum fixtures with versioned ops and tests for missing ops, storage width mismatch, item count/symbol/text/value mismatch and semantic-zero callback violations.
- [ ] Add decode tests for enum symbol/text/numeric success, unknown text/numeric failure, nested rollback and JSON input.
- [ ] Run focused targets and record RED because enum declarations are globally rejected.
- [ ] Parse enum declarations into bounded semantic metadata, resolve named enum fields before record resolution, and validate underlying scalar capability/ranges with checked conversion.
- [ ] Validate the complete native enum descriptor against schema metadata, then reuse it in the plan overlay so TurboUtils CBind owns assignment and rollback.
- [ ] Keep flags unsupported with `TBE_CBIND_UNSUPPORTED`, schema phase and declaration/field path.
- [ ] Run all TbeCBind focused and install/dependency tests.
- [ ] Commit Task 4 independently.

## Task 5: Generated sidecar parity for fixed scalars, UUID and enum

**Files:**

- Modify: `tbe/tbe_compiler/compiler_core.c`
- Modify: `tbe/tbe_compiler/templates/c_cbind_source.mustache`
- Modify: `tbe/tbe_compiler/templates/c_structs.mustache` only if generated enum metadata needs an existing annotation exposed
- Modify: `tbe/tbe_compiler/test_tbe_compiler.c`
- Modify: `tbe/tbe_compiler/test/cbind_sidecar.schema`
- Modify: `tbe/tbe_compiler/test/test_tbe_cbind_sidecar.c`
- Modify: `tbe/tbe_compiler/test/test_tbe_cbind_sidecar_cpp.cpp`
- Modify: `tbe/tbe_compiler/CMakeLists.txt`

- [ ] Extend compiler output assertions first for exact fixed-width CMeta symbols, removal of `long/size_t` ABI assertions, UUID descriptor, enum metadata/ops/data descriptor and flags rejection.
- [ ] Extend generated C and C++ consumers first to decode bool, all widths, UUID and enum through both descriptor accessors and `Type_from_cserde`; include failure rollback and Windows LLP64 coverage.
- [ ] Run compiler and sidecar targets; record RED at generation/compile due unsupported types and missing annotations.
- [ ] Replace compiler-local scalar validation/annotation branches with shared capability queries. Generate descriptor references from capability metadata, not a second switch.
- [ ] Generate immutable enum item/meta/storage ops/data descriptors. Call the public TurboUtils enum adapter contract; do not embed enum parsing in generated code.
- [ ] Reference the TurboUtils UUID descriptor and adapter; do not duplicate UUID parsing in the template.
- [ ] Verify runtime and generator support tables mechanically in one test: every shared scalar capability accepted by both, every non-goal rejected by both.
- [ ] Run compiler, C/C++ sidecar, standalone C/C++ and JSON reader integration tests.
- [ ] Commit Task 5 independently.

## Task 6: Dependency floor, documentation, benchmark smoke and full verification

**Files:**

- Modify: `CMakeLists.txt` or the existing dependency probe module selected by repository convention
- Modify: `tbe/tbe_cbind/README.md`
- Modify: `tbe/tbe_compiler/README.md`
- Modify: `tbe/tbe_cbind/test/benchmark_tbe_cbind.c` only for bounded smoke coverage, not a new benchmark framework
- Modify relevant install-consumer/dependency-negative fixtures under `tbe/tbe_cbind/test/`

- [ ] Add a configure-time compile/feature probe for required TurboUtils fixed-width, enum and UUID metadata. Test an intentionally old/missing fixture and assert configure fails with an actionable message.
- [ ] Update docs with the exact v2 matrix, enum-vs-flags distinction, UUID canonical input, native descriptor requirements, ownership/zero/rollback rules, and unchanged DataBind-free dependency graph.
- [ ] Add only a small scalar/enum/UUID decode benchmark smoke case if the existing benchmark structure can express it without distorting the CBind-vs-DataBind comparison.
- [ ] Build/install the TurboUtils prerequisite to an isolated prefix. Configure TurboParser against that prefix with `cmake --fresh --preset win-release-user` plus the discovered `TurboUtils_DIR`; never overwrite the shared package prefix.
- [ ] Run focused tests in order: capability, TbeCBind schema/plan/decode/JSON, compiler, generated C/C++ sidecars, standalone consumers, dependency-negative and install consumers.
- [ ] Run full Release build and CTest with `--output-on-failure`; record exact test counts and elapsed time.
- [ ] Run install/package verification, dependency closure, `git diff --check`, CodeGraph affected analysis, and confirm no `vendor/`, `.codegraph/`, SDD ledger/report or unrelated files are staged.
- [ ] Perform spec-compliance review, code-quality review and final whole-branch review. Resolve findings through the owning task implementer, then rerun affected and full verification.
- [ ] Commit documentation/verification changes, push `feat/tbe-cbind-scalar-enum`, and create a TurboParser PR linked to issue #5 and the TurboUtils prerequisite PR. Do not merge automatically.

## Plan self-review checklist

- [ ] Every in-scope family from the spec appears in runtime, generator and dependency tests.
- [ ] Every non-goal has an explicit stable rejection test.
- [ ] No task contains placeholder code, TODO/FIXME/HACK or partial public API.
- [ ] Descriptor names/types are consistent across TurboUtils header, shared capability rows, runtime fixtures and generated template.
- [ ] Ownership and zero-state are explicit for UUID and enum; integers/bool remain trivial values.
- [ ] C/C++ and LLP64 behavior are covered; no `long == int64_t` or `size_t == uint64_t` assumption remains.
- [ ] Rollback paths are tested after earlier owning and scalar fields have been written.
- [ ] The final dependency graph remains `TbeCBind -> TbeSchema + TurboUtils::CBind`, with no DataBind edge.
