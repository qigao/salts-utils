# Task 5 Report: 真实 SQLite 执行生成 DDL

## 变更文件

- `tbe/tbe_compiler/CMakeLists.txt`
- `tbe/tbe_compiler/test_tbe_database_ddl.c`
- `tbe/tbe_compiler/test_database_schema.schema`

## 结果概览

- `事实`：新增独立 TinyTest 集成 target `test_tbe_database_ddl`，仅测试侧链接 `unofficial::sqlite3::sqlite3`，未修改生产 `tbe_compiler` 或库 target 的 SQLite 依赖。
- `事实`：真实 `tbe_compiler_run()` 会读取 committed fixture `test_database_schema.schema`，生成临时 `.sql` 文件，再由 SQLite in-memory API 读取并执行该精确输出。
- `事实`：生产 DDL 首次真实执行即成功；首个运行时 RED 来自测试对 SQLite extended result code 的断言错误，而不是 DDL 本身错误。

## RED

### RED 1: 新集成测试首次运行

命令：

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && ctest --preset win-release-user -R test_tbe_database_ddl --output-on-failure"
```

结果摘录：

```text
1/1 Test #32: test_tbe_database_ddl ............***Failed
sql=INSERT INTO sessions (session_id, user_id, token) VALUES (100, 999, 'tok-duplicate');
error=UNIQUE constraint failed: sessions.session_id
Check failed: expected == 19 but got 1555
at ...\tbe\tbe_compiler\test_tbe_database_ddl.c:90
```

结论：

- `事实`：失败点发生在 DDL 已成功执行、catalog 已查询、有效 insert 已完成之后。
- `事实`：`1555` 是 SQLite extended constraint code；测试当时错误地把返回值当作主码 `19` 断言。
- `推论`：此次 RED 证明了新集成测试已真正打到 SQLite 运行时约束层，而不是只验证字符串输出。

## GREEN

### GREEN 1: 修正 SQLite extended result code 断言后

命令：

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target test_tbe_database_ddl --config Release && ctest --preset win-release-user -R test_tbe_database_ddl --output-on-failure"
```

结果摘录：

```text
[1/2] Building C object tbe\tbe_compiler\CMakeFiles\test_tbe_database_ddl.dir\test_tbe_database_ddl.c.obj
[2/2] Linking C executable bin\test_tbe_database_ddl.exe
1/1 Test #32: test_tbe_database_ddl ............   Passed
100% tests passed, 0 tests failed out of 1
```

### GREEN 2: 相邻回归

命令：

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target tbe_compiler test_tbe_compiler test_tbe_database_ddl --config Release && ctest --preset win-release-user -R ""tbe_compiler|tbe_database_ddl"" --output-on-failure"
```

结果摘录：

```text
ninja: no work to do.
1/2 Test #31: test_tbe_compiler ................   Passed
2/2 Test #32: test_tbe_database_ddl ............   Passed
100% tests passed, 0 tests failed out of 2
```

## SQLite 行为验证

- `事实`：生成 SQL 包含并成功创建 `users` 与 `sessions` 两张表。
- `事实`：`sqlite_master` 中 `users` 表 SQL 仍包含 `AUTOINCREMENT`。
- `事实`：`PRAGMA table_info("users")` 列顺序与类型为 `user_id/INTEGER`、`email/TEXT`、`display_name/TEXT`、`active/INTEGER`、`rank/INTEGER`。
- `事实`：`PRAGMA table_info("sessions")` 列顺序与类型为 `session_id/INTEGER`、`user_id/INTEGER`、`token/TEXT`。
- `事实`：插入 `users(email, display_name, active, rank)` 成功，回读得到 `generated_user_id > 0`，证明 identity 自动生成生效。
- `事实`：插入合法 `sessions(session_id, user_id, token)` 成功。
- `事实`：重复 `sessions.session_id` 被 SQLite 以 `SQLITE_CONSTRAINT_PRIMARYKEY` 拒绝。
- `事实`：重复 `users.email` 被 SQLite 以 `SQLITE_CONSTRAINT_UNIQUE` 拒绝。
- `事实`：显式重复 identity 主键 `users.user_id` 被 SQLite 以 `SQLITE_CONSTRAINT_PRIMARYKEY` 拒绝。
- `事实`：缺失 `users.display_name` 被 SQLite 以 `SQLITE_CONSTRAINT_NOTNULL` 拒绝。
- `事实`：`active = 2` 与 `rank = 256` 分别被 SQLite 以 `SQLITE_CONSTRAINT_CHECK` 拒绝。

## 清理与所有权

- `事实`：`sqlite_ddl_test_state_t` 统一持有 `sqlite3 *db`、`sqlite3_stmt *statement`、`char *sql_output_path`、`char *sql_text`、`char *sqlite_error`。
- `事实`：`sqlite_ddl_test_cleanup()` 是单一 cleanup 边界，负责：
  - `sqlite3_finalize(statement)`
  - `sqlite3_free(sqlite_error)`
  - `sqlite3_close(db)`
  - `free(sql_text)`
  - `tt_remove_file(sql_output_path)` 与 `free(sql_output_path)`
- `事实`：`sqlite_ddl_test_prepare()` 在每次 prepare 前先 finalize 旧 statement；`sqlite_ddl_test_exec()` 在每次 exec 前先释放旧 error string，避免路径间泄漏。

## 自审

- `事实`：本任务只改测试、fixture 与测试侧 CMake；生产编译器逻辑未改。
- `LOW` `推论`：`find_package(unofficial-sqlite3 CONFIG REQUIRED)` 当前放在 `tbe_compiler` 的测试区块内，和现有测试 target 一样随该目录配置；若未来仓库统一把测试 target 包进 `if(BUILD_TESTING)`，这里也应跟随收口。
