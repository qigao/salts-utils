# Native-reader migration: audited capability baseline

Task: #99; design: salts-utils PR #100; consumer: qigao/turbodb#52.
Source audit: salts-utils `615851439b939dc0692a410192e3bc79e0cec413`;
TurboDB consumer reference: `a37c1183ec53e4d1bb9a7b938b50d86d9688a3b2`.
This is the first prerequisite slice, not completion of the consumer inventory,
direct-reader interface, decoder, or migration.

## Actual reusable boundaries

| Existing code | Observation | Next reuse boundary |
| --- | --- | --- |
| `tbe_typed.c:typed_native_scalar_supported` | Canonical fixed-width numeric, enum-bits, bool8/UUID/fixed-byte paths are enumerated; ordinary C bool is skipped and generic buffer_ops is not admitted. | General native storage capability must be decided by the canonical graph/provider, with supported-kind policy tested explicitly. |
| `typed_native_record_preflight` | Checks exact CMeta fields/layout but requires a TBE overlay and nested overlay associations; presence is excluded. | Separate format-neutral graph validation from optional schema-overlay validation. Do not manufacture a second offset/type graph. |
| `typed_native_init_value` / `typed_native_clear_value` | Existing enum/fixed/Struct lifecycle can be identified, but these paths do not call general buffer init_zero/move/restore_zero. | Extend the shared lifecycle owner before admitting owned buffers; merely accepting the kind is unsafe. |
| `typed_native_from_json_scalar` | Native conversion still contains JSON-specific interpretation, including textual Boolean/numeric coercion. | Format interpretation must stay explicit; a direct canonical reader must not silently adopt every text-format coercion. |
| `data_bind_internal_parse_integer_magnitude` | Already called from canonical typed numeric/enum interpretation. | Reuse DataBind-owned checked numeric policy rather than introducing a CBind implementation/fallback. |
| `DataBindCore` CMake source list | Only format-provider orchestration is built into this target. | Target naming is not proof of a native conversion implementation or minimal runtime closure. |

The public `tbe_typed.h` inspected at this source revision has descriptor
validate/init/clear and format-based parse/serialize entry points, not the new
reader+graph+budget entry point requested by #99. No proposed public symbol is
introduced in this test-first commit.

## First executable requirement matrix

The new `test_data_bind_native_storage_requirements` calls the real existing
`tbe_typed_descriptor_validate/init/clear` boundary with valid canonical native
storage. It requires support for:

- existing fixed-width i32/u64, bool8 and double (positive controls);
- native C int/long/bool without replacing their semantic identities;
- owned text/bytes through the canonical buffer provider, including cleanup of
  actual allocated payload and idempotent clear;
- rejection of missing buffer operations and mismatched offsets without
  touching poisoned destination storage;
- native field offsets coming from CMeta rather than obsolete overlay fields.

These tests intentionally express the new migration requirement. A rejection
by the old documented typed slice is a missing capability, not evidence that
SQLite is broken or that the old public API violated its documented scope.
They do **not** invoke a direct CSerde reader. Exactly-one-value consumption,
preflight zero-read, source errors, token lifetimes, bounds during traversal,
atomic publication and native/dynamic equivalence still require separate tests.

No fixed pass/fail count is asserted before CI actually runs. The fixture prints
native name, actual DataBind status, path and diagnostic before asserting the
required capability, so rejection is distinguishable from an invalid fixture.
Provider callbacks are real Salts callbacks; the fixture does not implement a
decoder or supply a missing production lifecycle. Teardown frees only payload
that the test itself successfully assigned.

## Dependency and integration limits

The unchanged DataBind descriptor workflow pins Salts
`801202e58c2d86b35202414d4812e79a2fd25bae`. The recorded TurboDB ownership matrix
uses another Salts revision; successful separate builds must not be represented
as a same-version cross-library integration test. Future cutover must pin one
compatible dependency closure and check enum/buffer ABI contracts explicitly.

This first test deliberately links the existing DataBind aggregate, not just
DataBindCore. A test pass here would prove native storage behavior only, never
a Salts-only native-decoder link closure. The existing full TBE workflow builds
and runs the new ordinary CTest target; no new install/verification framework,
Python behavior test, fallback, or public API stub is added.

No TurboDB branch, prior ownership test or CBind production source is changed.
The 28-case post-construction owner suite remains a separate gate. The next
implementation should extend/partition the existing DataBind preflight and
lifecycle functions, not add another binder to satisfy the tests.
