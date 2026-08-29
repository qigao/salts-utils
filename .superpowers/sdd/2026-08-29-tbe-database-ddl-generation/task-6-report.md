# Task 6 Report

日期：2026-08-29

## 变更范围

- `tbe/tbe_compiler/compiler_core.c`
- `tbe/tbe_compiler/main.c`
- `tbe/tbe_compiler/test_tbe_compiler.c`
- `tbe/tbe_compiler/CLI_OPTIONS.md`
- `README.md`（仓库根原先不存在，按任务需要补了最小入口页）

## RED

先只加入测试，再验证它们按“旧行为”失败。

命令：

```powershell
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target test_tbe_compiler --config Release'
& 'C:/projects/cpp/turbonet/.worktrees/turbo-parser-db-schema/build/Msvc-Release/bin/test_tbe_compiler.exe' --filter 'explicit output'
& 'C:/projects/cpp/turbonet/.worktrees/turbo-parser-db-schema/build/Msvc-Release/bin/test_tbe_compiler.exe' --filter 'database languages before parsing'
```

关键输出：

```text
should require explicit output for SQLite and PostgreSQL DDL
[ FAIL ]
with info: language=sqlite stderr=Failed to read schema file: missing_database_output_contract.schema
Check failed: expected "...missing_database_output_contract.schema" to contain "--output"
```

```text
should reject source output with database languages before parsing
[ FAIL ]
with info: language=sqlite option=--source-output stderr=Failed to read schema file: missing_database_conflict_contract.schema
Check failed: expected "...missing_database_conflict_contract.schema" to contain "--source-output"
```

```text
should reject guest output with database languages before parsing
[ FAIL ]
...
should reject Lua output with database languages before parsing
[ FAIL ]
...
should reject DSL output with database languages before parsing
[ FAIL ]
...
```

结论：新增测试证明旧实现会先去读 schema，而不是先报 `--output`/冲突选项错误。

## GREEN

实现后先跑新增用例，再跑相关 CTest，最后全量 build + 全量 CTest。

命令：

```powershell
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target test_tbe_compiler --config Release'
& 'C:/projects/cpp/turbonet/.worktrees/turbo-parser-db-schema/build/Msvc-Release/bin/test_tbe_compiler.exe' --filter 'explicit output'
& 'C:/projects/cpp/turbonet/.worktrees/turbo-parser-db-schema/build/Msvc-Release/bin/test_tbe_compiler.exe' --filter 'database languages before parsing'
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target test_tbe_compiler test_tbe_database_ddl tbe_compiler --config Release'
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && ctest --preset win-release-user -R "tbe_compiler|tbe_database_ddl" --output-on-failure'
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --config Release'
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && ctest --preset win-release-user --output-on-failure'
```

关键输出：

```text
should require explicit output for SQLite and PostgreSQL DDL
[ OK ]
All tests passed
```

```text
should reject source output with database languages before parsing
[ OK ]
should reject guest output with database languages before parsing
[ OK ]
should reject Lua output with database languages before parsing
[ OK ]
should reject DSL output with database languages before parsing
[ OK ]
All tests passed
```

```text
Test #31: test_tbe_compiler .... Passed
Test #32: test_tbe_database_ddl . Passed
100% tests passed, 0 tests failed out of 2
```

```text
100% tests passed, 0 tests failed out of 36
Total Test time (real) =   2.73 sec
```

说明：第一次直接跑全量 `ctest --preset win-release-user` 时，因为只构建了少数 target，CTests 中大量可执行文件尚未生成而 `Not Run`；随后按 `cmake --build --preset win-release-user --config Release` 完整构建，再次执行全量 CTest 后全部通过。

## 真实 CLI 验证

使用已提交 fixture：`tbe/tbe_compiler/test_database_schema.schema`

命令：

```powershell
& 'build/Msvc-Release/bin/tbe_compiler.exe' 'tbe/tbe_compiler/test_database_schema.schema' --lang sqlite --output 'build/Msvc-Release/task6-cli-verify/schema.sqlite.sql'
& 'build/Msvc-Release/bin/tbe_compiler.exe' 'tbe/tbe_compiler/test_database_schema.schema' --lang postgresql --output 'build/Msvc-Release/task6-cli-verify/schema.postgresql.sql'
& 'build/Msvc-Release/bin/tbe_compiler.exe' 'tbe/tbe_compiler/test_database_schema.schema' --lang postgres --output 'build/Msvc-Release/task6-cli-verify/schema.postgres.sql'
```

结果：

```text
sqlite_sha256=A9B1C16F6A6851C0A58ECF96A30E562C2C2CD3C40F3CF8CC6700C6B2410766FE
postgresql_sha256=E452D1A1DE0F5232C142C086E34665B215FEBF0368931C3FBBF0BA6850C33F7A
postgres_alias_sha256=E452D1A1DE0F5232C142C086E34665B215FEBF0368931C3FBBF0BA6850C33F7A
postgres_alias_matches=True
```

SQLite 预览：

```sql
CREATE TABLE "users" (
  "user_id" INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
  "email" TEXT NOT NULL UNIQUE,
  "display_name" TEXT NOT NULL,
  "active" INTEGER NOT NULL DEFAULT 1 CHECK ("active" IN (0, 1)),
  "rank" INTEGER NOT NULL DEFAULT 0 CHECK ("rank" BETWEEN 0 AND 255)
);
CREATE TABLE "sessions" (
```

PostgreSQL 预览：

```sql
CREATE TABLE "users" (
  "user_id" bigint NOT NULL PRIMARY KEY GENERATED BY DEFAULT AS IDENTITY,
  "email" text NOT NULL UNIQUE,
  "display_name" text NOT NULL,
  "active" boolean NOT NULL DEFAULT TRUE,
  "rank" smallint NOT NULL DEFAULT 0 CHECK ("rank" BETWEEN 0 AND 255)
);
CREATE TABLE "sessions" (
```

SQLite 行为正确性已由现有 `test_tbe_database_ddl` 真实 SQLite API 集成测试覆盖；真实 CLI 这里补充验证了可执行文件端到端产物与 `postgres` alias 的逐字节等价性。

## 文档结果

- `tbe/tbe_compiler/CLI_OPTIONS.md`
  - 增补数据库语言 `sqlite` / `postgresql` / `postgres`
  - 明确数据库语言要求显式 `--output`
  - 明确 `--source-output` / `--guest-output` / `--lua-output` / `--dsl-output` 与数据库语言冲突
  - 增补完整 runnable schema、数据库命令、annotation contract、错误边界、类型映射、bootstrap-only 边界、自定义模板 marker 字段、运行时无 parser 依赖说明
- `README.md`
  - 新增仓库入口页
  - 在 TBE compiler 入口只保留高层说明和到详细文档的链接，不复制大表
  - 明确数据库 DDL 不是 migration engine

## 自审

- 未发现新的 `HIGH` / `MED` 问题。
- `compiler_core.c` 的改动保持在参数校验边界层，未改动数据库 IR、模板选择或既有 C 生成路径。
- `postgres` alias 仍只在 CLI 语言解析阶段映射到既有 `TBE_COMPILER_LANG_POSTGRESQL`，避免引入第二套模板或分支。

## Concerns

- 仓库根原先没有 `README.md`。本任务为满足“README 入口链接详细 compiler docs”的公开行为要求，新建了最小入口页；若上游另有预期 README 模板，需要后续统一风格时再整合。

## Fix Round 1

### 范围

- `tbe/tbe_compiler/compiler_core.c`
- `tbe/tbe_compiler/main.c`
- `tbe/tbe_compiler/test_tbe_compiler.c`

### RED

先只增加数据库语言下四个空字符串辅助输出冲突测试，再验证旧实现仍会先读 schema。

命令：

```powershell
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target test_tbe_compiler --config Release'
& 'C:/projects/cpp/turbonet/.worktrees/turbo-parser-db-schema/build/Msvc-Release/bin/test_tbe_compiler.exe' --filter 'should reject empty'
```

关键输出：

```text
should reject empty source output with database languages before parsing
[ FAIL ]
with info: language=sqlite option=--source-output stderr=Failed to read schema file: missing_database_empty_conflict_contract.schema
Check failed: expected "...missing_database_empty_conflict_contract.schema" to contain "--source-output"
```

```text
should reject empty guest output with database languages before parsing
[ FAIL ]
...
should reject empty Lua output with database languages before parsing
[ FAIL ]
...
should reject empty DSL output with database languages before parsing
[ FAIL ]
...
```

结论：旧实现把空字符串当作“未提供”，没有在数据库语言入口边界 fail fast。

### GREEN

实现后先重跑新增空字符串用例，再跑相邻回归和全量 CTest。

命令：

```powershell
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target test_tbe_compiler tbe_compiler --config Release'
& 'C:/projects/cpp/turbonet/.worktrees/turbo-parser-db-schema/build/Msvc-Release/bin/test_tbe_compiler.exe' --filter 'should reject empty'
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && ctest --preset win-release-user -R "tbe_compiler|tbe_database_ddl" --output-on-failure'
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && ctest --preset win-release-user --output-on-failure'
```

关键输出：

```text
should reject empty source output with database languages before parsing
[ OK ]
should reject empty guest output with database languages before parsing
[ OK ]
should reject empty Lua output with database languages before parsing
[ OK ]
should reject empty DSL output with database languages before parsing
[ OK ]
All tests passed
```

```text
Test #31: test_tbe_compiler .... Passed
Test #32: test_tbe_database_ddl . Passed
100% tests passed, 0 tests failed out of 2
```

```text
100% tests passed, 0 tests failed out of 36
Total Test time (real) =   1.38 sec
```

### 真实 CLI / Help 验证

命令：

```powershell
build/Msvc-Release/bin/tbe_compiler.exe missing.schema --lang sqlite --output build/Msvc-Release/task6-fix1-cli-verify/schema.sqlite.sql --source-output ""
build/Msvc-Release/bin/tbe_compiler.exe --help
```

关键输出：

```text
exit_code=1
output_exists=False
--source-output is supported only for the built-in C generator and cannot be combined with --lang sqlite
```

```text
-l, --lang: Target language (built-in template: c, cpp, cxx, go, rust, python, py, ts, typescript, sqlite, postgresql, postgres)
-o, --output: Output file path (required for sqlite/postgresql/postgres; default: stdout for other languages)
```

### 自审

- `MED` 已修复：数据库语言下四个辅助输出选项现在只要指针非 `NULL` 就会立即冲突，`""` 不再漏过入口校验。
- 现有非空冲突测试保留，C 语言原有 `--source-output` / `--guest-output` / `--lua-output` / `--dsl-output` 路径未改。
- `LOW` 已处理：CLI help 的 `--output` 文案已与 `sqlite` / `postgresql` / `postgres` 三个名字保持一致。
