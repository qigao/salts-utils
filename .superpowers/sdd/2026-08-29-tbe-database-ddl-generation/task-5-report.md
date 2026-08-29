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

## Fix Round 1

### 问题核实

- `事实`：TinyTest `check_*` 失败时会直接 `ttest_longjmp_fail__`，安装头 `C:/projects/cpp/external/pkgs/turboutils/release/include/tinytest.h:322-352` 明确在断言失败后调用 `ttest_longjmp_fail__(ttest_active_config__)`。
- `事实`：TinyTest 源码 `C:/projects/cpp/turbonet/turbo-utils/tinytest/src/tinytest.c:718` 会把 `after_each` hooks 绑定到每个 leaf test 的 `after_each_nodes`。
- `事实`：同一源码 `C:/projects/cpp/turbonet/turbo-utils/tinytest/src/tinytest.c:1178-1179` 在 `ttest_execute_target__` 之后遍历并执行 `after_each_nodes`，因此框架层负责在测试 body 失败后仍执行 cleanup hook。
- `事实`：原 `test_tbe_database_ddl.c` 使用函数局部 `sqlite_ddl_test_state_t state` 和 body 末尾 `cleanup:` 标签；一旦 `check_*` longjmp，body-local cleanup 不会运行。

### 实现

- `事实`：把 SQLite 集成测试状态迁到 `spec` 作用域 `static sqlite_ddl_test_state_t state`。
- `事实`：新增 `before_each()` 对该状态做零初始化；新增 `after_each()` 统一调用唯一的 `sqlite_ddl_test_cleanup(&state)`。
- `事实`：删除主集成测试 body-local `cleanup:` 标签和 `goto cleanup` 依赖，让资源回收只依赖 TinyTest fixture 生命周期。
- `事实`：新增 `it_should_fail("runs after_each cleanup after assertion longjmp")` 回归：先分配 temp path、SQL text、SQLite db、prepared statement、sqlite error，再故意触发断言失败。
- `事实`：新增后续测试 `it("observes fixture cleanup after the expected failure")`，断言 `after_each` 已运行一次，且在 cleanup 前观察到 5 个 live resources，证明 hook 在 expected-fail longjmp 后被执行。

### RED

命令：

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target test_tbe_database_ddl --config Release && ctest --preset win-release-user -R test_tbe_database_ddl --output-on-failure"
```

结果摘录：

```text
tbe_compiler SQLite DDL integration
  demonstrates the missing fixture cleanup on assertion longjmp
  [ XFAIL ]
  requires after_each-managed cleanup after the expected failure
  [ FAIL  ]
    Check failed: expected == 1 but got 0
```

结论：

- `事实`：`it_should_fail(...)` 本身按预期记为 `XFAIL`，suite 失败点来自其后的验证测试。
- `事实`：失败值 `0` 说明当时还没有任何 `after_each` 记录到该失败用例的 cleanup 运行。

### GREEN

命令 1：

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target test_tbe_database_ddl --config Release && ctest --preset win-release-user -R test_tbe_database_ddl --output-on-failure"
```

结果摘录：

```text
1/1 Test #32: test_tbe_database_ddl ............   Passed
100% tests passed, 0 tests failed out of 1
```

命令 2：

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

### CMake 守卫检查

- `事实`：执行

```powershell
rg.exe -n "BUILD_TESTING|if\(BUILD_TESTING\)|endif\(BUILD_TESTING\)|if\(ENABLE_TESTS\)" tbe/tbe_compiler tbe/schema tbe/data_bind -g "CMakeLists.txt"
```

返回空结果。

- `推论`：在这三个相邻目录内没有现成、统一的 build-test guard 风格可复用。
- `事实`：因此本轮没有把 `find_package(unofficial-sqlite3 CONFIG REQUIRED)` 或 `test_tbe_database_ddl` 包进新的单点守卫，避免制造与邻近测试不一致的例外风格。

### 本轮自审

- `事实`：本轮只修改 `tbe/tbe_compiler/test_tbe_database_ddl.c` 与同一报告文件。
- `事实`：资源所有权现在对 longjmp 明确：body 只获取资源，`after_each()` 统一释放，`sqlite_ddl_test_cleanup()` 保持幂等单入口。
- `LOW` `推论`：cleanup 回归依赖 TinyTest 的声明顺序执行 expected-fail 用例与后继观察用例；这对“验证 `after_each` 是否在上一用例失败后执行”是有意的顺序契约，不是业务状态耦合。

## Fix Round 2

### 问题复核

- `事实`：上一轮回归只在 `after_each()` 里记录了 cleanup 前的 live resource 数量，后继用例只断言该值为 `5`。
- `推论`：若 `sqlite_ddl_test_cleanup()` 退化成 no-op，该回归仍会通过，因为它从未检查 cleanup 后状态是否归零。
- `事实`：因此上一轮对“证明 cleanup 实际释放了全部资源”的表述过强；它只能证明 `after_each()` 在 longjmp 后被执行到了 cleanup 调用点之前。

### 实现

- `事实`：把 probe 结果拆成 `cleanup_probe_resource_count_before` 与 `cleanup_probe_resource_count_after` 两个计数。
- `事实`：`after_each()` 在 armed probe 路径下先记录 cleanup 前 live count，再调用唯一的 `sqlite_ddl_test_cleanup(&state)`，最后记录 cleanup 后 live count 并解除 probe。
- `事实`：后继观察用例现在同时断言：`after_each` 运行次数为 `1`、cleanup 前资源数为 `5`、cleanup 后资源数为 `0`。
- `事实`：`sqlite_ddl_test_cleanup()` 继续把 `statement`、`sqlite_error`、`db`、`sql_text`、`sql_output_path` 全部释放并置空，因此 post-count `0` 是真实后置条件，而非偶然默认值。

### 验证

- `事实`：本轮缺陷属于测试充分性问题；在修改断言契约前，现有可执行文件不会以运行时 RED 形式暴露该问题。缺陷证据来自代码复核，而不是新的行为失败。

命令 1：

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target test_tbe_database_ddl --config Release && ctest --preset win-release-user -R test_tbe_database_ddl --output-on-failure"
```

结果摘录：

```text
ninja: no work to do.
1/1 Test #32: test_tbe_database_ddl ............   Passed
100% tests passed, 0 tests failed out of 1
```

命令 2：

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

### 本轮自审

- `事实`：本轮只修改了 cleanup 回归的断言强度，没有触碰生产 DDL 生成逻辑。
- `事实`：当前回归同时覆盖了 longjmp 后 `after_each()` 被调用、cleanup 前确有 5 个已拥有资源、cleanup 后全部清零这三个条件。
- `LOW` `推论`：该验证仍依赖 `it_should_fail(...)` 与后继观察用例的执行顺序；本轮未额外引入更复杂机制去消除此顺序依赖，因为现有 TinyTest 行为已足以表达这项框架契约验证。
