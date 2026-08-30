# TBE Database DDL v2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 扩展 `tbe_compiler` 的 SQLite/PostgreSQL bootstrap DDL，使其支持同 schema 外键、普通/唯一复合索引、方言限定的自定义检查约束及建表后的 seed `INSERT`。

**Architecture:** TBE annotation parser 以兼容方式增加有序多参数：每个 annotation 继续暴露首参数 `value`，同时暴露包含全部参数的 `values`。数据库 builder 先建立表/列事实源，再解析 message 级关系和约束，最后解析 schema 级初始化语句；模板只排列已验证、已引用的 IR，不读取原始 annotation。

**Tech Stack:** C11、Lemon、TBE schema Node tree、Mustache、TinyTest、SQLite3 C API、CMake Presets/CTest。

**Spec:** `docs/architecture/tbe-database-ddl-generation.md`

## Global Constraints

- 旧单参数 annotation 的 `value`、顺序和既有生成输出必须保持兼容。
- 外键和索引引用 TBE message/field 名，由数据库 builder 映射 `db_table`/`db_column`，不得接受原始 SQL 标识符片段。
- `db_check(name, dialect, expression)` 只接受 `sqlite` 或 `postgresql`；表达式必须非空、引号/括号闭合，且不含分号或 SQL 注释边界。
- `db_init(dialect, statement)` 只接受不带结尾分号的单条 `INSERT`；模板统一补分号并在所有表和索引之后输出。
- 失败必须停止生成且不留下半成品输出；不得增加运行时数据库连接或 TurboDB 依赖。
- 每项生产行为先由真实 parser/compiler/SQLite 的失败测试定义，再实现最小代码使其通过。

---

### Task 1: 多参数 annotation 兼容 AST

**Files:**

- Modify: `tbe/schema/parser/schema_grammar.y`
- Modify: `tbe/schema/test/test_tbe_parser.c`

**Interfaces:**

- Consumes: 现有 `attr_item ::= IDENT LPAREN value RPAREN` 语法和 Node tree ownership。
- Produces: 每个 annotation map 保留 `name`、首参数 `value`，并新增有序字符串列表 `values`。

- [x] **Step 1: 写失败测试**

  在 `test_tbe_parser.c` 解析：

  ```c
  const char *schema =
      "[db_index(lookup, tenant, email)] message User { int64 tenant; string email; }";
  ```

  断言 `value == "lookup"`，`values == {"lookup", "tenant", "email"}`；另断言 `[id(1)]` 仍有相同 `value` 且单元素 `values`。

- [x] **Step 2: 验证 RED**

  重新生成/构建 `test_tbe_parser` 并运行精确 CTest；预期多参数 schema 解析失败。

- [x] **Step 3: 最小实现**

  在 Lemon grammar 中增加 `attr_values`/`attr_value`，把 IDENT、NUMBER、STRING 统一复制到 `values` list；创建 attribute 时复制第一个字符串到既有 `value`，并把 list ownership 转入 attribute map。

- [x] **Step 4: 验证 GREEN**

  构建并运行 `test_tbe_parser`、`test_tbe_compiler`，确认新旧 annotation 均通过。

### Task 2: 规范化数据库关系、索引、检查与 initializer IR

**Files:**

- Modify: `tbe/tbe_compiler/database_schema.c`
- Modify: `tbe/tbe_compiler/test_tbe_compiler.c`

**Interfaces:**

- Consumes:

  ```tbe
  [db_foreign_key(fk_order_user, User, user_id, id)]
  [db_index(idx_order_lookup, tenant, user_id)]
  [db_unique_index(uidx_order_number, tenant, order_number)]
  [db_check(ck_total, sqlite, "total_cents >= 0")]
  message Order { ... }

  schema Shop [db_init(sqlite, "INSERT INTO users(id) VALUES (1)")];
  ```

  复合外键继续按 local/remote field 成对追加：

  ```tbe
  db_foreign_key(fk_membership_tenant, Tenant, tenant_id, id, region_id, region_id)
  ```

- Produces:
  - schema: `db_tables`, `db_indexes`, `db_initializers`
  - table: `db_foreign_keys`, `db_checks`
  - foreign key: `sql_constraint_name`, `sql_referenced_table_name`, `db_foreign_key_columns`
  - index: `sql_index_name`, `sql_table_name`, `db_index_columns`, optional `is_unique_index`
  - initializer: `sql_statement`

- [x] **Step 1: 写 IR 失败测试**

  用真实 `parse_schema` + `tbe_database_schema_build` 断言 SQLite/PostgreSQL IR 中的引用表/列、复合列顺序、索引唯一标记、方言过滤、quoted constraint/index 名和 seed 顺序。

- [x] **Step 2: 验证 RED**

  构建运行 `test_tbe_compiler`；预期 builder 以“annotation invalid”失败。

- [x] **Step 3: 建立两阶段 builder**

  第一阶段保留现有 table/column 构建并记录 source message/field 到独立 builder metadata；第二阶段解析 message annotations，解析 schema `db_init`。所有输出字符串由 IR 独占，失败统一走 cleanup。

- [x] **Step 4: 实现结构校验**

  - `db_foreign_key`: 参数数至少 4 且 `2 + 2*n`；目标 message 必须带 `db_table`；字段存在且未 ignore；本地/目标 SQL 类型一致；目标列组必须对应 PK、`db_unique(1)` 或 `db_unique_index`。
  - `db_index`/`db_unique_index`: 至少 name + 1 field；索引名全 schema 唯一；字段存在且未 ignore；同一索引内字段不重复。
  - `db_check`: 恰好 name/dialect/expression；约束名在表内唯一；只把当前 dialect 项写入 IR。
  - `db_init`: 恰好 dialect/statement；只把当前 dialect 项写入 IR；statement 必须以独立关键字 `INSERT` 开始。

- [x] **Step 5: 验证 GREEN**

  运行 IR 测试并确认全部新节点、顺序与 ownership 行为通过。

### Task 3: SQL 安全边界和错误语义

**Files:**

- Modify: `tbe/tbe_compiler/database_schema.c`
- Modify: `tbe/tbe_compiler/test_tbe_compiler.c`

**Interfaces:**

- Consumes: Task 2 的四类 annotation。
- Produces: `TBE_DATABASE_SCHEMA_STATUS_INVALID_SCHEMA` 和包含 annotation/message/field 的 bounded diagnostic。

- [x] **Step 1: 写 table-driven 失败测试**

  覆盖错误参数数量、未知 dialect、空名称/表达式/statement、重复 constraint/index、未知或 ignored field/message、重复列、奇数外键列参数、外键类型不匹配、非 unique target、分号、`--`/`/*`/`*/`、未闭合引号/括号、非 INSERT 初始化语句。

- [x] **Step 2: 验证 RED**

  运行 `test_tbe_compiler`，确认每组测试因缺少对应校验而失败，而不是 parser 或 fixture 错误。

- [x] **Step 3: 最小安全校验实现**

  实现有界的 SQL fragment scanner：跟踪单引号、双引号和括号深度，识别 SQL doubled quote；拒绝控制字符、分号和注释边界。initializer 在 scanner 通过后再做 ASCII 大小写无关的首关键字 `INSERT` 校验。

- [x] **Step 4: 验证 GREEN**

  重跑所有 negative cases，确认 diagnostic 指向声明所在 message/schema 和具体 annotation。

### Task 4: 确定性模板输出与真实 SQLite 行为

**Files:**

- Modify: `tbe/tbe_compiler/templates/sqlite_schema.mustache`
- Modify: `tbe/tbe_compiler/templates/postgresql_schema.mustache`
- Modify: `tbe/tbe_compiler/test_tbe_compiler.c`
- Modify: `tbe/tbe_compiler/test_tbe_database_ddl.c`
- Modify: `tbe/tbe_compiler/test_database_schema.schema`

**Interfaces:**

- Consumes: Task 2 的 normalized IR。
- Produces: SQLite table constraints 内联于 `CREATE TABLE`；PostgreSQL 外键在表和索引之后通过 `ALTER TABLE` 创建，以支持前向引用和 unique-index target；所有 seed `INSERT` 最后输出。

- [x] **Step 1: 写 SQLite/PostgreSQL golden 失败测试**

  手写精确输出，覆盖复合 FK、普通/唯一复合索引、自定义 CHECK、方言过滤和两个有序 seed INSERT。

- [x] **Step 2: 验证 RED**

  构建运行 `test_tbe_compiler`；预期输出缺少新 DDL。

- [x] **Step 3: 修改两个 Mustache 模板**

  只读取规范化 IR；表级 constraint 始终以前导逗号追加，索引和 initializer 用各自 `has_next_*` 控制确定性换行。

- [x] **Step 4: 写并验证 SQLite 集成行为**

  先扩展真实 SQLite 测试，启用 `PRAGMA foreign_keys=ON`，断言：合法 seed 已存在、孤儿外键失败、CHECK 失败、复合唯一索引拒绝重复、普通索引可从 `sqlite_master` 查询。确认测试先失败，再用 Task 2/模板实现使其通过。

- [x] **Step 5: 运行 focused GREEN**

  精确运行 `test_tbe_compiler` 和 `test_tbe_database_ddl`。

### Task 5: 文档、自定义模板与回归验证

**Files:**

- Modify: `docs/architecture/tbe-database-ddl-generation.md`
- Modify: `tbe/tbe_compiler/CLI_OPTIONS.md`
- Modify: `README.md`
- Verify: `tbe/tbe_compiler/CMakeLists.txt`

**Interfaces:**

- Consumes: 最终 annotation 与 IR 契约。
- Produces: 可复制运行的 v2 schema 示例、IR 字段说明、限制与安全边界。

- [x] **Step 1: 更新文档**

  删除“v1 仅 CREATE TABLE”的过期描述，记录四类 annotation 的参数、方言、顺序、失败条件、同 schema 外键限制及 seed 仅 INSERT 限制。

- [x] **Step 2: 自定义模板回归**

  增加一个 custom template 行为测试，渲染 FK/index/check/initializer 的结构化字段，证明不需要读取 raw attributes。

- [x] **Step 3: 聚焦与相邻回归**

  运行：

  ```text
  cmake --build --preset win-release-user --target test_tbe_parser test_tbe_compiler test_tbe_database_ddl tbe_compiler --parallel
  ctest --preset win-release-user -R ^test_tbe_parser$ --output-on-failure
  ctest --preset win-release-user -R ^test_tbe_compiler$ --output-on-failure
  ctest --preset win-release-user -R ^test_tbe_database_ddl$ --output-on-failure
  ```

- [x] **Step 4: 全量与静态交付检查**

  构建全部目标并运行完整 CTest；随后执行 `codegraph sync .`、`codegraph affected`、`git diff --check` 和 `git status --short`。若安装资源未变化，不修改 preset 或安装规则。
