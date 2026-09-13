# DataBind-Only Boundary Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the duplicate native binder from every active SaltsUtils dependency and consumer path while preserving DataBind and schema/CMeta behavior.

**Architecture:** DataBind remains SaltsUtils' sole binding engine. CMeta supplies semantic/structural metadata, the schema layer supplies wire overlays, CSTL supplies container storage, and Salts parsers/CSerde supply format mechanics. This slice removes an unused external binding dependency; it does not introduce a replacement facade or change the upstream Salts package.

**Tech Stack:** C11, CMake 3.27+, Salts CMeta/CSTL/CSerde, DataBind, TinyTest, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-09-12-databind-convergence-design.md`

## Global Constraints

- DataBind is the only public binding engine owned or consumed by SaltsUtils.
- Preserve the structural-metadata versus schema-overlay boundary.
- Do not add a feature flag, fallback, forwarding facade or compatibility path.
- Do not modify or remove the upstream Salts target in this project.
- Invalid or unsupported mappings continue to fail through explicit DataBind/CMeta status values.
- Do not claim a local CMake baseline: this execution environment has no `cmake` executable.

---

### Task 1: Add the dependency-boundary RED

**Files:**
- Create: `tbe/schema/test/test_databind_only_dependency_contract.cmake`
- Modify: `tbe/schema/CMakeLists.txt`

**Interfaces:**
- Consumes: the checked-out SaltsUtils source tree at `PROJECT_SOURCE_DIR`.
- Produces: CTest `test_databind_only_dependency_contract`, which rejects active `.c`, `.h`, `.cpp`, `.hpp`, `.mustache`, `CMakeLists.txt` and `.cmake` references to the duplicate binder API/target while excluding itself, build output, planning history and the exact top-level CI dependency checkouts `salts/` and `vcpkg/`.

- [ ] **Step 1: Write the failing contract test**

```cmake
string(CONCAT FORBIDDEN_TARGET "Salts::C" "Bind")
string(CONCAT FORBIDDEN_INCLUDE "<c" "bind/")
string(CONCAT FORBIDDEN_MACRO "C" "BIND_")
string(CONCAT FORBIDDEN_SYMBOL "c" "bind_")

file(GLOB_RECURSE POLICY_FILES LIST_DIRECTORIES FALSE
  "${PROJECT_SOURCE_DIR}/CMakeLists.txt"
  "${PROJECT_SOURCE_DIR}/*.cmake"
  "${PROJECT_SOURCE_DIR}/*.c"
  "${PROJECT_SOURCE_DIR}/*.h"
  "${PROJECT_SOURCE_DIR}/*.cpp"
  "${PROJECT_SOURCE_DIR}/*.hpp"
  "${PROJECT_SOURCE_DIR}/*.mustache")

foreach(FILE_PATH IN LISTS POLICY_FILES)
  file(RELATIVE_PATH RELATIVE_FILE_PATH "${PROJECT_SOURCE_DIR}" "${FILE_PATH}")
  if(FILE_PATH STREQUAL CMAKE_CURRENT_LIST_FILE OR
     RELATIVE_FILE_PATH MATCHES "^(salts|vcpkg)/" OR
     RELATIVE_FILE_PATH MATCHES "(^|/)(build[^/]*|install|bin|out|cmake-build-[^/]*|\\.vcpkg_installed|vcpkg_installed|conan-cache)/" OR
     RELATIVE_FILE_PATH MATCHES "^docs/superpowers/")
    continue()
  endif()
  file(READ "${FILE_PATH}" CONTENT)
  foreach(FORBIDDEN IN ITEMS "${FORBIDDEN_TARGET}" "${FORBIDDEN_INCLUDE}"
                             "${FORBIDDEN_MACRO}" "${FORBIDDEN_SYMBOL}")
    string(FIND "${CONTENT}" "${FORBIDDEN}" POSITION)
    if(NOT POSITION EQUAL -1)
      message(FATAL_ERROR "DataBind-only dependency violation: ${FILE_PATH}")
    endif()
  endforeach()
endforeach()
```

- [ ] **Step 2: Register the contract in the schema test directory**

```cmake
add_test(NAME test_databind_only_dependency_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPROJECT_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/test/test_databind_only_dependency_contract.cmake")
```

- [ ] **Step 3: Verify RED**

Run locally where CMake is available:

```bash
ctest --test-dir build/linux-gcc-debug/tbe/schema --no-tests=error \
  -R '^test_databind_only_dependency_contract$' --output-on-failure
```

Expected: FAIL naming `tbe/schema/CMakeLists.txt`,
`tbe/schema/test/test_schema_cmeta_buffer.c` or the installed Cron consumer.
In the current environment, push this test-only commit and require the exact-head
schema workflow to demonstrate the same failure before Task 2.

- [ ] **Step 4: Commit the RED**

```bash
git add tbe/schema/CMakeLists.txt \
  tbe/schema/test/test_databind_only_dependency_contract.cmake
git commit -m "test(databind): reject duplicate binder dependencies"
```

### Task 2: Remove active schema and package-consumer dependencies

**Files:**
- Modify: `tbe/schema/CMakeLists.txt`
- Modify: `tbe/schema/test/test_schema_cmeta_buffer.c`
- Modify: `tbe/schema/src/schema_cmeta_buffer.h`
- Modify: `cron/tests/package_consumer/CMakeLists.txt`
- Modify: `tbe/tbe_compiler/templates/c_structs.mustache`

**Interfaces:**
- Consumes: `schema_cmeta_buffer_data`, CMeta buffer provider descriptors and the existing DataBind export.
- Produces: schema/provider tests and installed package consumers with no duplicate binding target, header, macro or symbol dependency.

- [ ] **Step 1: Remove the foreign decoder fixture**

Delete the CSerde token fixture and the `CBind consumption of lowered storage`
test group from `test_schema_cmeta_buffer.c`. Retain these behaviors:

```c
check_true(cmeta_data_desc_valid(&owned));
check_true(cmeta_data_desc_valid(&borrowed));
check_true(cmeta_type_identity_equal(data.storage_type->identity, &identity));
check_equal(cmeta_data_buffer_assign(&data, &out, input, sizeof(input),
                                     sizeof(input)), CMETA_OK);
check_equal(cmeta_data_buffer_restore_zero(&data, &out), CMETA_OK);
```

- [ ] **Step 2: Remove the test-only target link**

Keep `test_schema_cmeta_buffer.c` and `test_schema_cmeta_profiles.c` as
`test_schema_cmeta` sources, but delete the external target from
`target_link_libraries` rather than replacing it with another binder.

- [ ] **Step 3: Make the installed Cron consumer test only Cron**

```cmake
add_executable(cron_package_consumer main.c)
target_link_libraries(cron_package_consumer PRIVATE Salts::Cron)
```

- [ ] **Step 4: Remove stale source comments**

State that buffer lifecycle belongs to the selected CMeta provider and that the
generated graph seam does not itself promise native decode/encode behavior.

- [ ] **Step 5: Verify GREEN**

Run:

```bash
ctest --test-dir build/linux-gcc-debug/tbe/schema --no-tests=error \
  -R '^(test_schema_cmeta|test_databind_only_dependency_contract)$' \
  --output-on-failure
```

Expected: PASS. Also build and run the installed Cron package consumer.

- [ ] **Step 6: Commit**

```bash
git add tbe/schema cron/tests/package_consumer/CMakeLists.txt \
  tbe/tbe_compiler/templates/c_structs.mustache
git commit -m "refactor(databind): remove duplicate binder dependency"
```

### Task 3: Correct the current public architecture

**Files:**
- Modify: `README.md`
- Modify: `ARCHITECTURE.md`
- Modify: `tbe/TBE_AS_GENERAL_SCHEMA.md`
- Modify: `tbe/tbe_compiler/CLI_OPTIONS.md`
- Modify: `tbe/data_bind/README.md`
- Modify: `tbe/data_bind/CMETA_SCHEMA_CAPABILITIES.md`

**Interfaces:**
- Consumes: current exported targets and the approved DataBind-only spec.
- Produces: documentation that directs native, generated and dynamic binding users to `Salts::DataBind` and keeps CMeta/schema responsibilities distinct.

- [ ] **Step 1: Remove obsolete retirement and replacement guidance**

Document that DataBind is built, installed and exported, and that generated C
links `Salts::DataBind`. Remove claims that DataBind is legacy, excluded from the
package or waiting on another binding engine.

- [ ] **Step 2: Document the canonical graph boundary**

Use this ownership statement consistently:

```text
CMeta: native structure and semantic type graph
schema overlay: external names, presence/defaults, wire layout and validation
DataBind: native/dynamic conversion, rollback and format orchestration
CSTL: concrete container storage
CSerde/parsers: format tokens and mechanics
```

- [ ] **Step 3: Run the dependency gate and documentation scans**

```bash
ctest --test-dir build/linux-gcc-debug/tbe/schema --no-tests=error \
  -R '^test_databind_only_dependency_contract$' --output-on-failure
rg -n 'legacy DataBind|retired DataBind|DataBind runtime.*no longer' \
  README.md ARCHITECTURE.md tbe
```

Expected: CTest PASS and `rg` returns no obsolete current guidance.

- [ ] **Step 4: Commit**

```bash
git add README.md ARCHITECTURE.md tbe
git commit -m "docs(databind): make DataBind the sole binding engine"
```

### Task 4: Verify the exact branch head

**Files:**
- Modify only if verification identifies a regression directly caused by Tasks 1-3.

**Interfaces:**
- Consumes: the exact committed head from Tasks 1-3.
- Produces: reproducible local or GitHub Actions evidence for schema, enum, descriptor, parser and installed-consumer gates.

- [ ] **Step 1: Run focused targets**

```bash
cmake --build build/linux-gcc-debug --target \
  test_schema_cmeta test_data_bind_schema_reflection_contract \
  test_data_bind_cmeta_reflection test_tbe_typed_cmeta_graph -j2
ctest --test-dir build/linux-gcc-debug --no-tests=error \
  -R 'schema_cmeta|data_bind.*reflection|tbe_typed_cmeta_graph|databind_only' \
  --output-on-failure
```

- [ ] **Step 2: Run the broader TBE suite and install consumer**

```bash
cmake --build build/linux-gcc-debug --target tbe/all install -j2
ctest --test-dir build/linux-gcc-debug/tbe --no-tests=error --output-on-failure
cmake --build build/native-consumer -j2
ctest --test-dir build/native-consumer --no-tests=error --output-on-failure
```

- [ ] **Step 3: Require exact-head GitHub Actions**

Require successful runs for DataBind direct parsers, schema boundaries, enum
conformance and descriptor safety. Record run IDs and exact head SHA; do not use
earlier main or PR evidence.

If any gate fails, stop and return to the task that owns the failing file. Add a
focused regression there before changing production code; do not fold an unknown
CI correction into the verification task.

### Task 5: Reconcile GitHub issues

**Files:**
- No repository files.

**Interfaces:**
- Consumes: the approved spec and exact-head GREEN evidence.
- Produces: current issue bodies and closure reasons matching the DataBind-only architecture.

- [ ] **Step 1: Close stale work**

Close #5 as `not_planned` because its referenced implementation is absent and its
ownership direction is superseded. Close #50 as `completed` only after confirming
the merged main repair runs.

- [ ] **Step 2: Rewrite the master and child scopes**

Update #8, #46, #47 and #48 so DataBind owns native and dynamic conversion over
one CMeta graph. Remove dependencies on the external binder and remove every
fallback/compatibility migration clause.

- [ ] **Step 3: Rewrite adjacent work**

Update #6, #7 and #9 so adapters, fuzzing and benchmarks target active DataBind,
schema, CSerde and parser paths.

- [ ] **Step 4: Publish evidence**

Add the exact head, run IDs, tested scopes and remaining independent work to #8.
Do not close #8/#46/#47/#48/#6/#7/#9 until their rewritten acceptance criteria
are implemented and GREEN.

### Task 6: Prepare the next implementation slice

**Files:**
- Create: `docs/superpowers/plans/2026-09-13-databind-cmeta-native-runtime.md`

**Interfaces:**
- Consumes: #47's rewritten acceptance criteria and the generated CMeta graph APIs.
- Produces: a separate TDD plan that makes DataBind's native conversion consume CMeta structural descriptors while schema-only metadata stays in its overlay.

- [ ] **Step 1: Inventory duplicated typed metadata**

Map `TbeTypedField` members to either CMeta structural data or DataBind wire
overlay data. Name every call site that reads structural fields directly.

- [ ] **Step 2: Define the first failing public/generated consumer test**

Require generated native parse/serialize to validate against the public CMeta
graph, including a copied-but-semantically-equal scalar descriptor and a mismatched
native offset. The valid case must pass; the mismatch must return
`DATA_BIND_ERR_SCHEMA` atomically.

- [ ] **Step 3: Write the full RED/GREEN plan**

Keep native runtime work independent from dynamic-container migration (#46) and
file splitting (#48), so each change has one rejection boundary and its own exact
CI evidence.

- [ ] **Step 4: Commit**

```bash
git add docs/superpowers/plans/2026-09-13-databind-cmeta-native-runtime.md
git commit -m "docs(databind): plan canonical native CMeta runtime"
```
