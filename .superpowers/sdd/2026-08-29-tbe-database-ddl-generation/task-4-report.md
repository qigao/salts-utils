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

## Fix Round 1

### 修复结果

- `HIGH`：普通的单列 `db_primary_key(1)` 现在在列约束中发出 `PRIMARY KEY`；identity 主键仍只发出一次。SQLite 与 PostgreSQL 各自增加独立 golden，DDL 的 `PRIMARY KEY` 约束保证重复值不能被该表接受。
- `HIGH`：临时输出改为同目录 `<output>.tbe.<uuid-v4>.tmp`。UUID 来自 TurboUtils CSPRNG，创建使用 Windows `_open(..., _O_CREAT | _O_EXCL)` 或 POSIX `open(..., O_CREAT | O_EXCL)`；没有 check-then-open。仅在本调用独占创建成功后记录所有权并允许 cleanup unlink。预先存在的旧式同名 `.tbe.tmp` 不会被触碰。
- `HIGH`：渲染、flush、close 和 rename 失败前均不修改目标；成功后以 `turbo_fs_rename()` 替换。`turbo_fs.h` 的契约明确为“replacing an existing destination file”；实现 `turbo_fs.c:1262-1268` 在 Windows 使用 `MoveFileExA(..., MOVEFILE_REPLACE_EXISTING)`，POSIX 使用 `rename()`。Windows 锁定目标的回归测试确认替换失败时已存在目标仍为原字节内容；未采用删除目标再 rename 的不原子方案。
- `MED`：恢复共享 Mustache provider 对空 `NODE_STRING` 的原真值语义。数据库 IR 以正向、作用域专用的 `has_next_column`、`has_next_primary_key`、`has_next_table` 和 `has_sql_constraints` 驱动模板分隔符/可选约束，不再依赖空 `is_last` 或全局语义变更。新增回归确认空字符串 section 输出 `present`。

### Round 1 RED

命令：

```powershell
cmd.exe /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 & cmake --build --preset win-release-user --target test_tbe_compiler & ctest --test-dir build\Msvc-Release -R ^test_tbe_compiler$ --output-on-failure'
```

输出：构建成功，CTest 预期失败：`58 passed, 5 failed`。失败分别是空字符串 section 输出为空、SQLite/PostgreSQL 单列普通主键缺少 `PRIMARY KEY`、旧固定 `.tbe.tmp` 报“Temporary output file already exists”，以及初始不正确的模板失败测试。后者改为确定性的悬空关闭标签后可稳定进入 `mustache_compile()` 失败路径。

### Round 1 GREEN

命令：

```powershell
cmd.exe /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 & cmake --build --preset win-release-user --target test_tbe_compiler tbe_compiler & ctest --test-dir build\Msvc-Release -R ^test_tbe_compiler$ --output-on-failure & powershell -NoProfile -Command "if ((Test-Path build/Msvc-Release/bin/templates/sqlite_schema.mustache) -and (Test-Path build/Msvc-Release/bin/templates/postgresql_schema.mustache)) { Write-Output TEMPLATE_RESOURCES_OK } else { Write-Error TEMPLATE_RESOURCES_MISSING; exit 1 }"'
```

输出：`1/1 Test #31: test_tbe_compiler Passed`、`100% tests passed, 0 tests failed out of 1`、`TEMPLATE_RESOURCES_OK`。

### Round 1 Mutation

临时将普通单列主键追加 token 从 `PRIMARY KEY` 改为 `UNIQUE`，运行与 Round 1 GREEN 相同的 `test_tbe_compiler` 构建/CTest 命令。

输出：预期失败，`59 passed, 4 failed`；两份独立 dialect golden、定制 DB-IR 模板 golden 和临时文件成功路径 golden 都报告期望 `PRIMARY KEY`、实际 `UNIQUE`。随后立即使用 `apply_patch` 恢复 `PRIMARY KEY`；该变异不在提交中。

### 自审与残余关注点

- `事实`：TurboUtils 当前公开 `turbo_fs_open` 标志仅含 `RDONLY/WRONLY/RDWR/CREAT/TRUNC/APPEND`，没有独占创建标志（`turbo_fs.h:310-315`）；故唯一的无竞态实现是在 UUID 同目录名上调用平台原子独占创建。命名熵与 `O_EXCL`/`_O_EXCL` 共同保证并发调用不会共享临时 inode；现有 `.tbe.tmp` 保留测试覆盖了可重复的碰撞前提。
- `事实`：新增测试覆盖预存临时文件保留、模板编译失败时原目标保留、Windows 目标替换失败时原目标保留，以及两方言普通单列主键 DDL。全部 `test_tbe_compiler` 用例通过。
- `LOW`：Windows 专属替换失败测试使用 `CreateFileA` 的零共享锁来稳定复现 `MoveFileExA` 失败；POSIX 替换语义已由 TurboUtils 的 `rename()` 实现和同一 `turbo_fs_rename` 公开契约核对，但本 Windows 工作树未执行 POSIX 运行时测试。
