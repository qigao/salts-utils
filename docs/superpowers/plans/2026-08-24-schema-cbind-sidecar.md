# Schema CBind Sidecar Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 从 TBE schema 构建期生成可直接消费 CSerde reader 的 CMeta/CBind semantic sidecar，同时对当前无法安全表达的 schema 语义 fail fast。

**Architecture:** `tbe_compiler` 复用现有 schema AST 和 typed annotations，通过独立 opt-in template 生成 immutable CMeta descriptors 与薄 `Type_from_cserde` façade；TBE wire/layout metadata 保持独立。

**Tech Stack:** C11, CMake, Mustache, TurboParser JSON parser/DataBind, TurboUtils CMeta/CSerde/CBind, TinyTest.

**Spec:** `docs/superpowers/specs/2026-08-24-schema-cbind-sidecar.md`

## Global Constraints

- 不修改 `vendor/`。
- 不改变默认生成结果或现有 `TbeTypedType` ABI。
- unsupported schema 必须在生成期或目标 ABI 静态断言处明确失败。
- 每个行为改动先写失败测试，再写最小实现。
- 只提交 `.codegraph/` 之外的任务相关文件。

---

## Task 1: 锁定 CLI 与生成契约

- [ ] 在 `test_tbe_compiler.c` 添加 `--cbind-output` 对应 core option 的失败测试：生成内容、`[name]`/`[c]`、缺少 source output、alias/optional/unsupported type。
- [ ] 运行最小 compiler test，确认因 option/API 尚不存在而失败。
- [ ] 向 `tbe_compiler_options_t`、CLI parsing 与 compiler core 添加 opt-in path、约束和清晰错误。
- [ ] 只运行 compiler test，确认转绿。

## Task 2: 生成 immutable CMeta/CBind sidecar

- [ ] 添加 Mustache 输出快照断言，覆盖 scalar、string、nested object descriptor 和 façade 声明。
- [ ] 运行最小测试，确认模板尚不存在导致失败。
- [ ] 给 schema fields 增加只服务 sidecar 的 semantic annotations 和支持矩阵校验。
- [ ] 新增 `c_cbind_source.mustache`，生成 layout reflection、semantic descriptors、ABI static assertions 与 `Type_from_cserde`。
- [ ] 将模板加入 build/install resource 清单。
- [ ] 运行 compiler tests，确认生成文本与 fail-fast 行为。

## Task 3: 生成代码端到端验证

- [ ] 新增专用 schema 与 TinyTest，先引用尚未生成的 `*_cbind_data` / `*_from_cserde`，观察 build/test 失败。
- [ ] 扩展 CMake custom command 生成 sidecar，测试目标链接 `TurboUtils::CBind` 与 JSON parser。
- [ ] 覆盖 JSON `[name]` key、`[c]` member、nested record、owned string、descriptor validity、失败回滚。
- [ ] 运行专用生成测试和 JSON CSerde reader tests。

## Task 4: 文档、回归与交付

- [ ] 更新 tbe_compiler/DataBind 用户文档，说明用途、所有权、命令和 v1 支持矩阵。
- [ ] 运行格式检查、相关构建与 CTest；确认工作树无 `vendor/` 与 `.codegraph/` 变更。
- [ ] 复核 diff 与公开 API 兼容性，提交并 push 当前 feature branch。
- [ ] 更新现有 PR 描述，明确 schema sidecar 范围与验证证据。
