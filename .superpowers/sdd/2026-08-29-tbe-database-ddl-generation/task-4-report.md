# Task 4：确定性 SQLite/PostgreSQL DDL 渲染

## 结果

- 内置 `sqlite` 与 `postgresql` 模板均从已校验、独立拥有的数据库 IR 渲染；`--template` 在数据库语言下使用同一 IR，而非原始 schema AST。
- SQLite 与 PostgreSQL 的逐字节 golden 覆盖引用标识符、单列 identity 主键、复合主键、nullable、UNIQUE、DEFAULT、bool/unsigned CHECK 与 dialect 类型差异。
- 输出先写入同目录 `<output>.tbe.tmp`，渲染、flush、close 成功后以 `turbo_fs_rename()` 替换目标；失败会移除临时文件，已有目标不会在渲染失败时被截断。
- 两份模板已进入 `TBE_COMPILER_TEMPLATE_FILES`，因此同时成为 link dependency、post-build copy 的输入，并随现有 `templates` 目录安装。

## TDD 证据

### RED

命令：

```powershell
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --preset win-release-user && cmake --build --preset win-release-user --target test_tbe_compiler && ctest --preset win-release-user -R ^test_tbe_compiler$ --output-on-failure'
```

结果：`test_tbe_compiler` 失败，新增 SQLite、PostgreSQL 和自定义数据库模板三项均返回 `1`；stderr 是预期的 `Built-in database generation for --lang ... is not available yet`。其余 52 项通过。

### GREEN（首次）

命令：

```powershell
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target test_tbe_compiler && ctest --preset win-release-user -R ^test_tbe_compiler$ --output-on-failure'
```

结果：`100% tests passed, 0 tests failed out of 1`。

### Mutation

临时将 SQLite signed-integer 映射从 `INTEGER` 改为 `BROKEN_INTEGER`，运行：

```powershell
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target test_tbe_compiler && ctest --preset win-release-user -R ^test_tbe_compiler$ --output-on-failure'
```

结果：预期失败（3 个断言失败），其中新的 SQLite golden 明确报告 `INTEGER` 与 `BROKEN_INTEGER` 不一致。随后立即以 `apply_patch` 恢复为 `INTEGER`；该变异不在提交中。

### 恢复后的最终验证

命令：

```powershell
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target tbe_compiler test_tbe_compiler && ctest --preset win-release-user -R ^test_tbe_compiler$ --output-on-failure'
```

结果：`tbe_compiler` 与 `test_tbe_compiler` 均链接成功，`1/1 Test #31: test_tbe_compiler Passed`，CTest 报告 `100% tests passed`。

资源复制检查：

```powershell
$templatePaths = @('build\Msvc-Release\bin\templates\sqlite_schema.mustache', 'build\Msvc-Release\bin\templates\postgresql_schema.mustache'); foreach ($templatePath in $templatePaths) { if (-not (Test-Path -LiteralPath $templatePath)) { Write-Error "Missing built template: $templatePath"; exit 1 } }; Write-Output 'Built templates: sqlite_schema.mustache, postgresql_schema.mustache'
```

输出：`Built templates: sqlite_schema.mustache, postgresql_schema.mustache`。

`git diff --check` 无输出。

## 变更文件

- `tbe/tbe_compiler/compiler_core.c`
- `tbe/tbe_compiler/database_schema.c`
- `tbe/tbe_compiler/mustache_helpers.c`
- `tbe/tbe_compiler/CMakeLists.txt`
- `tbe/tbe_compiler/templates/sqlite_schema.mustache`
- `tbe/tbe_compiler/templates/postgresql_schema.mustache`
- `tbe/tbe_compiler/test_tbe_compiler.c`

## 自审与关注点

- `事实`：数据库 IR 的 `is_last` 在嵌套 Mustache scope 中会向父 table 回退。非末元素现在带空字符串标记，且 provider 将空字符串视为 false，保证字段、主键和 table 各自的逗号/分隔判断不受父 scope 干扰；完整 `test_tbe_compiler` 回归已通过。
- `事实`：原有 `test_tbe_compiler.c` 在 MSVC Release 构建中仍打印 C4702 unreachable-code warnings；本任务的最终 CTest 成功，且 warning 位于已有 Lua 相关测试路径，未因本改动新增失败。
- `LOW`：本任务未执行真实 SQLite API 或 PostgreSQL 容器验证；这是后续 Task 5/7 的指定范围。当前以两份逐字节 golden 约束渲染契约。
