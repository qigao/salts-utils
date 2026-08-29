# TBE Database DDL Generation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 `tbe_compiler` 从带数据库 annotation 的 TBE schema 确定性生成可执行的 SQLite/PostgreSQL bootstrap DDL。

**Architecture:** 在现有 parser/annotator 之后增加独立数据库 schema IR 构建与 fail-fast 校验层，公共层处理 annotation、标识符、主键和默认值，dialect 策略仅处理类型与 identity 差异；Mustache 模板只排列已归一化 SQL token。TurboDB/ORM/CFlow 运行时不改动、不新增 TurboParser 依赖。

**Tech Stack:** C11、TBE schema parser、Node tree、Mustache、TinyTest、SQLite3 C API、CMake Presets/CTest。

**Spec:** `docs/architecture/tbe-database-ddl-generation.md`

## Global Constraints

- 保持现有 C/C++/Go/Rust/Python/TypeScript 输出及枚举值不变。
- 每个生产改动前先提交一个能观察到目标行为的失败测试，并确认失败原因正确。
- 数据库模式失败时不回退到通用 SQL 类型，也不产生部分成功输出。
- 不引入 TurboDB、PostgreSQL client 或新的运行时依赖。
- `.codegraph/`、build tree 与生成 SQL 不提交。

---

## Task 1: 固化 CLI language contract

**Files:**

- Modify: `tbe/tbe_compiler/test_tbe_compiler.c`
- Modify: `tbe/tbe_compiler/compiler_core.h`
- Modify: `tbe/tbe_compiler/main.c`

- [x] 在 `test_tbe_compiler.c` 增加 language 解析/枚举行为测试，手工断言 `sqlite`、`postgresql`、`postgres` 的结果及既有枚举值。
- [x] 构建并运行 `test_tbe_compiler`，确认测试因缺少数据库语言支持而失败。
- [x] 在既有语言值之后追加 SQLite/PostgreSQL 枚举，并把 CLI choice/alias 映射到它们；抽取可由测试直接调用的纯解析函数，CLI 只负责参数边界。
- [x] 重跑 focused test，确认新 language contract 通过且既有 alias 不变。

## Task 2: 以失败测试定义规范化数据库 IR

**Files:**

- Modify: `tbe/tbe_compiler/test_tbe_compiler.c`
- Create: `tbe/tbe_compiler/database_schema.h`
- Create: `tbe/tbe_compiler/database_schema.c`
- Modify: `tbe/tbe_compiler/CMakeLists.txt`

- [ ] 添加一个最小 annotated schema fixture，经真实 parser/annotator 后调用数据库 IR builder；逐项断言表名、列名、必填/可空、主键顺序、unique、identity、default 和 dialect type token。
- [ ] 分别为 SQLite/PostgreSQL 添加手工推导的类型映射表测试，覆盖 bool、signed/unsigned widths、float/double、string、bytes、uuid 和 enum underlying type。
- [ ] 构建并运行，确认测试因 IR API 不存在而失败；为保证 RED 可编译，先只声明接口和测试所需错误枚举，不添加成功实现。
- [ ] 实现最小 database IR ownership：builder 独占新 Node tree，失败释放全部派生节点，destroy 可重复处理 NULL；不得借用会在 AST 释放后失效的字符串。
- [ ] 实现公共 annotation 解析、SQL identifier 双引号转义、列名去重、主键序号排序及 dialect 类型策略。
- [ ] 重跑 focused test，确认基础 IR 与类型映射变绿。

## Task 3: 覆盖 fail-fast 数据契约

**Files:**

- Modify: `tbe/tbe_compiler/test_tbe_compiler.c`
- Modify: `tbe/tbe_compiler/database_schema.c`

- [ ] 增加 table-driven negative tests：无 `db_table`、重复 table/column、空表、重复/缺口/零主键序号、optional 主键、identity 非单列整数主键、identity+default/unique、非法 boolean annotation 值、`db_ignore` 冲突。
- [ ] 增加 unsupported field tests：collection、map、group、composite/union 引用失败，而 `db_ignore(1)` 允许跳过。
- [ ] 增加 default tests：字符串单引号转义、bool、signed/unsigned 边界、float、enum 常量；类型不匹配与越界必须失败。
- [ ] 逐组运行确认新用例先按目标原因失败，再实现最小校验；错误结果包含 dialect、message、field、annotation 或 type 上下文。
- [ ] 完成后运行全部 `test_tbe_compiler`，确认无旧行为回归。

## Task 4: 生成确定性 SQLite/PostgreSQL DDL

**Files:**

- Create: `tbe/tbe_compiler/templates/sqlite_schema.mustache`
- Create: `tbe/tbe_compiler/templates/postgresql_schema.mustache`
- Modify: `tbe/tbe_compiler/test_tbe_compiler.c`
- Modify: `tbe/tbe_compiler/compiler_core.c`
- Modify: `tbe/tbe_compiler/CMakeLists.txt`

- [ ] 先添加两种 dialect 的完整 golden output tests，覆盖 quoted identifiers、单/复合主键、nullable、unique、default、range CHECK 与 identity。
- [ ] 运行 focused test，确认 compiler 尚未构建数据库 IR/解析模板而失败。
- [ ] 在 `tbe_compiler_run` 中只对数据库语言构建 IR，选择内置模板并渲染；自定义 `--template` 也必须收到相同 IR。
- [ ] 增加两份只消费归一化字段的 Mustache 模板，确保逗号、换行与表顺序确定，不输出 `IF NOT EXISTS`。
- [ ] 将模板加入 build copy、link dependency 与 install resource 列表。
- [ ] 重跑 golden tests；手工变异一种类型映射或移除一个约束，确认至少一个测试会失败后恢复。

## Task 5: 用真实 SQLite 执行生成 DDL

**Files:**

- Create: `tbe/tbe_compiler/test_tbe_database_ddl.c`
- Create: `tbe/tbe_compiler/test_database_schema.schema`
- Modify: `tbe/tbe_compiler/CMakeLists.txt`

- [ ] 新增 TinyTest 集成测试 target，链接仓库现有 vcpkg SQLite target，不修改生产 target 依赖。
- [ ] 测试调用真实 compiler core 生成临时 SQL，再用 SQLite API 在内存数据库执行；确认表/列存在、合法数据可插入，NOT NULL/UNIQUE/CHECK 违规被数据库拒绝。
- [ ] 先让测试因数据库语言尚未完整接线或 DDL 约束错误而失败，再修到通过。
- [ ] 用 TinyTest 临时文件 helper/统一 cleanup 释放 schema、SQL、SQLite handle，任何 setup 失败都报告明确断言。

## Task 6: 文档、安装与公开行为

**Files:**

- Modify: `tbe/tbe_compiler/CLI_OPTIONS.md`
- Modify: `README.md`
- Modify: `tbe/tbe_compiler/main.c`
- Modify: `tbe/tbe_compiler/compiler_core.c`
- Modify: `tbe/tbe_compiler/test_tbe_compiler.c`

- [ ] 更新 CLI 文档：命令、annotation、类型映射、v1 bootstrap 边界、失败情况和可运行示例。
- [ ] 在 README 的 TBE compiler 入口增加数据库 DDL 链接，不把它描述为 migration engine。
- [ ] 添加数据库语言与 C-only auxiliary outputs 冲突测试，再实现入口校验和可操作错误消息。
- [ ] 构建 `tbe_compiler`，运行真实 CLI 分别生成 SQLite/PostgreSQL 文件并对输出做行为验证。

## Task 7: 回归、安装消费与远程 PostgreSQL

**Files:**

- Verify only unless测试暴露缺陷。

- [ ] 执行 `cmake --build --preset win-release-user --target test_tbe_compiler test_tbe_database_ddl tbe_compiler --config Release`。
- [ ] 执行 `ctest --preset win-release-user -R "tbe_compiler|tbe_database_ddl" --output-on-failure`，随后运行全量 `ctest --preset win-release-user --output-on-failure`。
- [ ] 执行 `cmake --build --preset install-win-release-user --config Release`，从安装目录运行两种数据库语言，证明模板随包安装。
- [ ] 按 TurboDB EU 远程 runbook 在临时 PostgreSQL 容器执行生成 DDL，验证 identity、numeric uint64、unique/not-null/check；容器和临时 volume 必须使用本任务专属名字并在确认目标后清理。
- [ ] 运行 `codegraph sync .` 与 `codegraph affected`，检查受影响测试候选并补跑遗漏项。
- [ ] 检查 `git diff --check`、`git status --short`、变更统计和公开文档一致性；记录无法执行的验证及残余风险。
