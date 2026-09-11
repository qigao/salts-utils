# TBE #30 Descriptor Layout Validation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `TbeTypedDescriptor` a fail-fast ABI boundary that rejects unsafe host and fixed-wire layouts before parse, serialize, initialization, or cleanup can use descriptor metadata.

**Architecture:** Keep the existing `TbeTypedType` / `TbeTypedField` ABI unchanged. Strengthen the existing descriptor validator for host-storage invariants and the binary-layout validator for wire invariants, using checked half-open intervals `[offset, offset + extent)` and existing derived extent helpers. Invalid metadata returns `DATA_BIND_ERR_SCHEMA`; no fallback, inference, or DataBind module refactor is introduced.

**Tech Stack:** C11, TinyTest, CMake/CTest, AddressSanitizer on the existing Linux developer preset.

**Spec:** GitHub issue #30 (`fix(tbe): harden typed binary descriptor layout validation`).

## Global Constraints

- Preserve the current public `TbeTypedType`, `TbeTypedField`, and `TbeTypedDescriptor` layouts.
- Treat descriptor metadata as untrusted at the public descriptor/binary boundary.
- Reject invalid layouts before allocation, read, write, copy, or cleanup based on invalid metadata.
- Keep failed direct binary decode transactional.
- Do not combine this change with #8 DataBind source-file refactoring.
- Do not add compatibility fallback for malformed descriptors.
- Use checked `size_t` arithmetic; touching/adjacent half-open intervals are valid, overlapping intervals are not.

---

### Task 1: Establish the RED boundary

**Files:**
- Test: `tbe/data_bind/test_tbe_typed_descriptor_safety.c`
- Modify: `tbe/data_bind/CMakeLists.txt`

**Interfaces:**
- Consumes: `tbe_typed_validate_descriptor()`, `tbe_typed_serialize_binary_into()`.
- Produces: deterministic regression coverage for every #30 invariant without performing unsafe writes.

- [x] **Step 1: Add adversarial descriptor tests**

Cover presence wire overflow, overlapping owning host fields, presence/owning overlap, map key/value overlap, fixed-wire overlap, fixed-wire/presence overlap, `wire_size` mismatch, and adjacent positive-control ranges.

- [x] **Step 2: Register the focused CTest target**

```cmake
cmake_add_test(test_tbe_typed_descriptor_safety
  SOURCES test_tbe_typed_descriptor_safety.c
  LIBS ${DATA_BIND_TEST_LIBS}
  FOLDER "tbe/data_bind/tests")
```

- [ ] **Step 3: Run RED**

Run on a repository-capable Linux environment:

```sh
cmake --preset linux-dev-user
cmake --build build/linux-gcc-debug --target test_tbe_typed_descriptor_safety
ctest --test-dir build/linux-gcc-debug -R '^test_tbe_typed_descriptor_safety$' --output-on-failure
```

Expected: the invalid descriptors fail their assertions because current validation returns `DATA_BIND_OK` or reaches `DATA_BIND_ERR_BUFFER_TOO_SMALL` instead of `DATA_BIND_ERR_SCHEMA`; the adjacent positive control passes.

- [ ] **Step 4: Record the RED output in issue #30 before production changes**

### Task 2: Harden host descriptor validation

**Files:**
- Modify: `tbe/data_bind/tbe_typed.c` around `typed_field_host_extent()`, `typed_value_host_extent()`, and `typed_validate_descriptor_at()`.
- Test: `tbe/data_bind/test_tbe_typed_descriptor_safety.c`.

**Interfaces:**
- Consumes: existing derived host extents and `typed_size_fits()`.
- Produces: descriptor validation that rejects unsafe host ownership aliasing.

- [ ] **Step 1: Keep the existing RED host-overlap tests failing before editing production**

The production change that makes them pass is interval-overlap validation; do not alter the expected status.

- [ ] **Step 2: Add one half-open interval predicate**

```c
static int typed_ranges_overlap(size_t left_offset, size_t left_size,
                                size_t right_offset, size_t right_size) {
  size_t left_end;
  size_t right_end;
  if (!typed_add_fits(left_offset, left_size, &left_end) ||
      !typed_add_fits(right_offset, right_size, &right_end))
    return 1;
  return left_offset < right_end && right_offset < left_end;
}
```

Zero-sized ranges do not overlap. If an end cannot be represented, validation treats the range as invalid/overlapping rather than wrapping.

- [ ] **Step 3: Reject map key/value overlap**

After deriving `value_extent`, reject overlap between `[map_key_offset, map_key_offset + sizeof(tstr))` and `[map_value_offset, map_value_offset + value_extent)`.

- [ ] **Step 4: Reject owning host field overlap and presence/owning overlap**

During descriptor validation, compare only fields whose cleanup owns/reaches owned storage: `STRING`, `BYTES`, `LIST`, `SET`, `MAP`, `OBJECT`, and fixed arrays whose element kind is owning/object. Reject pairwise overlap between those host ranges; also reject any such range overlapping the host presence bitmap.

- [ ] **Step 5: Run the focused test**

Expected: host/map overlap cases turn GREEN; wire-layout cases remain RED until Task 3.

### Task 3: Harden fixed binary layout validation

**Files:**
- Modify: `tbe/data_bind/tbe_typed.c` around `typed_field_wire_extent()` and `typed_validate_layout_at()`.
- Test: `tbe/data_bind/test_tbe_typed_descriptor_safety.c`.

**Interfaces:**
- Consumes: `typed_field_wire_extent()`, `typed_size_fits()`, interval predicate from Task 2.
- Produces: deterministic fixed-block layout contract.

- [ ] **Step 1: Reject a presence wire range larger than the fixed block**

The current wire representation places the presence bitmap at wire offset zero, so require:

```c
if (type->presence_size > type->fixed_block_size)
  return typed_error(error, DATA_BIND_ERR_SCHEMA, type->name,
                     "Typed presence bitmap exceeds the fixed wire block");
```

- [ ] **Step 2: Make `wire_size` exact for fixed wire fields**

After deriving `wire_extent`, require `field->wire_size == wire_extent`. Do not infer or repair mismatched metadata.

- [ ] **Step 3: Reject fixed wire fields overlapping the presence range**

For each fixed field interval, reject overlap with `[0, presence_size)`.

- [ ] **Step 4: Reject pairwise fixed wire overlap**

Compare each fixed field with later fixed fields using their derived extents. Adjacent intervals such as `[0,2)` and `[2,4)` remain valid.

- [ ] **Step 5: Run focused test**

Expected: all #30 descriptor-safety tests pass.

### Task 4: Document the ABI/layout invariants

**Files:**
- Modify: `tbe/data_bind/tbe_typed.h` around `TbeTypedField`, `TbeTypedType`, `TbeTypedDescriptor`, and binary API comments.

**Interfaces:**
- Produces: public contract matching runtime validation without changing ABI.

- [ ] **Step 1: Document host and wire interval rules**

State that fixed wire ranges and the wire presence range are non-overlapping, `wire_size` is the exact derived fixed-field extent, owning host storage cannot alias other owned storage/presence storage, and malformed descriptors return `DATA_BIND_ERR_SCHEMA`.

- [ ] **Step 2: Do not add new public fields or version the ABI for documentation-only clarification**

### Task 5: Verification and integration gate

**Files:**
- Verify: `tbe/data_bind/test_tbe_typed_descriptor_safety.c`
- Verify: `tbe/data_bind/test_tbe_typed.c`
- Verify: generated typed descriptor consumer tests reachable through the existing CTest suite.

**Interfaces:**
- Produces: exact-head evidence suitable for a focused PR closing #30.

- [ ] **Step 1: Build focused targets under the Linux developer/ASan profile**

```sh
cmake --preset linux-dev-user
cmake --build build/linux-gcc-debug --target test_tbe_typed_descriptor_safety test_tbe_typed
```

- [ ] **Step 2: Run focused CTest with sanitizer enabled**

```sh
ctest --test-dir build/linux-gcc-debug -R '^(test_tbe_typed_descriptor_safety|test_tbe_typed)$' --output-on-failure
```

Expected: PASS, no ASan diagnostics.

- [ ] **Step 3: Run the broader DataBind/TBE tests available in the configured build**

```sh
ctest --test-dir build/linux-gcc-debug -R '(data_bind|tbe)' --output-on-failure
```

Expected: PASS.

- [ ] **Step 4: Review the exact diff against `main`**

Allowed product files for #30: `tbe/data_bind/tbe_typed.c`, `tbe/data_bind/tbe_typed.h`; test/CMake/plan files as above. No parser/compiler feature work from #31-#34.

- [ ] **Step 5: Commit, open a focused PR referencing `Fixes #30`, and record exact-head test evidence in #30**
