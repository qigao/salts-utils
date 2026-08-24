# TBE CBind Runtime Plan Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现完全独立于 DataBind 的 runtime TBE schema -> immutable CMeta/CBind plan，使任意 CSerde reader 可事务式填充已有 C struct。

**Architecture:** 新增 opt-in static target `TurboParser::TbeCBind`。factory 私有使用 `TurboParser::TbeSchema` 解析 schema，以 schema `[name]`/`[c]` 与调用方 CMeta native shape 构建 plan-owned descriptor overlay；执行层只调用 `TurboUtils::CBind`。

**Tech Stack:** C11, CMake, TurboParser TbeSchema, TurboUtils CMeta/CSerde/CBind, TinyTest.

**Spec:** `docs/superpowers/specs/2026-08-24-tbe-cbind-runtime-plan.md`

## Global Constraints

- `tbe/tbe_cbind` 的 production target、header 与 source 不得依赖、include 或调用 DataBind。
- 不修改 `vendor/`，不提交 `.codegraph/`。
- 不改变现有 DataBind、TbeSchema、tbe_compiler sidecar 或默认 target 行为。
- schema-only 且无 native CMeta shape 必须 fail fast；不得创建动态值 fallback。
- 所有 TbeCBind-owned 可增长结构必须有 options 硬上限与 checked arithmetic；现有
  TbeSchema parser 临时 AST 仅受 schema byte hard cap 间接约束，不把
  `max_plan_bytes` 误写为 create 全过程内存预算。
- 每个行为先写失败测试，再写最小实现；验证从最小测试逐步扩大。

---

## Task 1: 锁定独立 target 与公开契约

**Files:**

- Create: `tbe/tbe_cbind/CMakeLists.txt`
- Create: `tbe/tbe_cbind/include/tbe_cbind/tbe_cbind.h`
- Create: `tbe/tbe_cbind/src/tbe_cbind.c`
- Create: `tbe/tbe_cbind/test/test_tbe_cbind_public_api.c`
- Modify: `tbe/CMakeLists.txt`

- [ ] 先添加 public API compile test，引用 opaque plan、versioned options/error、options/error init、create/destroy/shape/decode；确认因 target/header 不存在而失败。
- [ ] 添加 `tbe_cbind` static target，PRIVATE link `TurboParser::TbeSchema`，PUBLIC link `TurboUtils::CBind`，安装并导出为 `TurboParser::TbeCBind`。
- [ ] 添加仅做 argument/options version/size/limit 校验的最小 factory skeleton；失败必须保持 `*out == NULL`，不得暴露未完成成功路径。
- [ ] 在 configure-time test 中检查 target dependency graph 不含 `TurboParser::DataBind`；public header 必须自包含所需标准/CBind/CMeta 类型、提供 `extern "C"`，并由 C 与 C++ executable 实际 link/run，而不只是编译 translation unit。
- [ ] 运行 public API test，确认转绿；用 `rg.exe` 检查 production files 不含 DataBind symbol/header。

## Task 2: 提取有界 schema semantic model

**Files:**

- Create: `tbe/tbe_cbind/src/tbe_cbind_internal.h`
- Create: `tbe/tbe_cbind/src/schema_model.c`
- Create: `tbe/tbe_cbind/test/test_tbe_cbind_schema.c`
- Modify: `tbe/tbe_cbind/src/tbe_cbind.c`

- [ ] 添加失败测试：invalid syntax、空 type name、embedded NUL、slice 无 caller NUL、unknown type、multiple/empty/unportable `[name]`、mapped/mapped 与 mapped/canonical semantic collision、unsupported declaration/attribute/type，以及每个 schema/type/field/depth/name/plan byte limit；添加同时使用 `[name]`/`[c]` 的正例。
- [ ] 在调用 `parse_schema` 前以 checked `schema_size + 1` 创建 bounded NUL-terminated 副本；type name 同样按显式长度处理，失败不得读取 slice 之外字节。
- [ ] 私有创建 `Node` root 并调用 `parse_schema`；不得把 Node 放入 public header 或 READY plan。
- [ ] 单次遍历构建有界 type index 和临时 semantic model，所有 count/size 运算先检查 overflow，再分配。
- [ ] 明确测试 `max_types/max_fields/max_depth/max_name_bytes` 在 AST 提取时拒绝，`max_plan_bytes` 只统计 READY plan；不得断言现有 parser 临时 AST 受这些 limit 约束。
- [ ] 只接受 v1 支持矩阵；遇到 alias/optional/default/union/enum/container 等立即返回稳定 phase/path/status。
- [ ] plan 创建结束前释放 schema AST；测试在覆盖/释放输入 schema buffer 后继续查询 plan，证明不借用文本或 Node。
- [ ] 运行 schema 单测、TbeSchema parser/robustness 相邻回归。

## Task 3: 编译 schema/native CMeta overlay

**Files:**

- Create: `tbe/tbe_cbind/src/native_shape.c`
- Create: `tbe/tbe_cbind/src/plan_builder.c`
- Create: `tbe/tbe_cbind/test/test_tbe_cbind_plan.c`
- Modify: `tbe/tbe_cbind/src/tbe_cbind_internal.h`
- Modify: `tbe/tbe_cbind/src/tbe_cbind.c`

- [ ] 添加手写 CMeta native fixtures 和失败测试，覆盖 root kind、malformed layout、multiple/empty/invalid `[c]`、mapped/mapped 与 mapped/canonical native collision、missing native member、semantic-only layout（不得按 declaration order 猜测）、offset/size/alignment/type mismatch、extra native field、嵌套 mismatch 与非法 buffer adapter。
- [ ] 只使用 public CMeta API 与 v1 mapping rules 检查 native/overlay 自洽；按 `[c]` 在 layout 中找 member，再用 offset 关联 native data field。不得 include `cbind/src/internal.h` 或复制 private CBind validator；权威 CBind graph/resource preflight 仍由每次 `cbind_decode` 在消费 reader 前执行。
- [ ] 构建 plan-owned reflected-layout + data-descriptor 双 overlay：两边 field name 都使用 `[name]`，native offset/size/alignment/type/storage/buffer ops 保持借用；nested struct 递归构建。不得把 semantic data fields 直接挂到原始 native layout。
- [ ] 用单一 cleanup 路径释放临时 model 与部分 allocation；只有完整成功后原子发布 READY plan。
- [ ] 实现 `plan_shape` 与 destroy；测试深层失败/OOM injection 不泄漏且 `out == NULL`。
- [ ] 运行 plan test，并运行 TurboUtils CMeta/CBind descriptor/preflight 相关测试。

## Task 4: 接入 CBind 事务执行

**Files:**

- Create: `tbe/tbe_cbind/test/test_tbe_cbind_decode.c`
- Modify: `tbe/tbe_cbind/src/tbe_cbind.c`

- [ ] 先添加 CSerde token reader fixtures 的失败测试：scalar、nested、`[name]`/`[c]`、owned/borrowed string、borrowed transient 拒绝、destination non-zero、unknown/duplicate/missing/range/limit/source error 与 rollback。
- [ ] 实现 `tbe_cbind_plan_decode` 薄 façade，只校验 READY plan 后调用 `cbind_decode(context, plan_shape, reader, out, error)`；不转换 CBind 错误。
- [ ] 验证 `cbind_error.shape/field` 指向 plan-owned overlay 时仅在 plan 生命周期内有效，并在 public API 文档写明诊断指针失效点。
- [ ] 测试 plan 在多线程只读共享，每个线程使用独立 context/scratch、reader、out、error 与仓库标准 reentrant buffer adapter；destroy 仅在 join 后调用。自定义 callback 有共享可变状态时要求调用方外部串行化。
- [ ] 运行 decode tests 与 TurboUtils CBind transaction/buffer/struct 相邻回归。

## Task 5: JSON 端到端与安装消费者

**Files:**

- Create: `tbe/tbe_cbind/test/test_tbe_cbind_json.c`
- Create or modify: repository install-consumer fixture selected during implementation
- Modify: `tbe/tbe_cbind/CMakeLists.txt`

- [ ] 先添加只链接 `TurboParser::TbeCBind` 与 JSON CSerde provider 的 consumer，确认缺少实现或 export 时失败；不得 include DataBind header。
- [ ] 从 runtime schema 建 plan，将 JSON reader 解码到已有 CMeta struct；直接复用 spec 的合法 `[c(...), name(...)] type field;` schema，覆盖 nested record、renamed member、owning string、schema buffer 释放和 reader/DOM 生命周期。
- [ ] borrowed JSON 路径覆盖 transient slice 拒绝、stable slice 成功、DOM/backing owner 在 native clear 前保持存活，并只在 clear 后释放；区分 reader wrapper 与 backing owner。
- [ ] 安装 TurboParser 后在独立 build tree `find_package` 并链接 `TurboParser::TbeCBind`；检查 link command/import dependency 不出现 DataBind。
- [ ] 运行专用 JSON test、install consumer 与 JSON CSerde reader 回归。

## Task 6: 文档、性能基线与交付

**Files:**

- Modify: `ARCHITECTURE.md`
- Modify: relevant TBE/DataBind user documentation selected during implementation
- Create: `tbe/tbe_cbind/test/benchmark_tbe_cbind.c`

- [ ] 文档化 CBind、TbeCBind、DataBind 与 build-time sidecar 的选择边界、desired usage、支持矩阵、所有权、semantic-zero、线程与错误语义。
- [ ] 添加 creation 与 repeated decode 两组 benchmark；以 generated sidecar direct CBind 为 decode baseline，不把结果描述为机器码 JIT。
- [ ] 运行格式检查、最小 tests、相邻回归、完整 build/CTest、install consumer；可用时运行 ASan/TSan。
- [ ] 用 `rg.exe` 复核 production TbeCBind 无 DataBind 引用；用 CMake graph/link output 复核 direct/transitive dependency。
- [ ] 检查 diff 不含 `vendor/`、`.codegraph/` 或无关修改，提交 feature branch 并请求 code/design review。
- [ ] 根据审查修正后重新验证，push 并创建独立 PR；PR 描述列出 DataBind-independence 证据、测试命令、兼容性与残余风险。
