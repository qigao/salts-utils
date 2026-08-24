# Task 5 实现报告：JSON 端到端与安装消费者

日期：2026-08-24

分支：`design/cbind-runtime-schema`

提交：单个 `Task 5: add TbeCBind JSON install consumer coverage` 提交；最终哈希随任务回报提供。

## 结果

- `事实`：新增 `test_tbe_cbind_json`，目标只直接链接
  `TurboParser::TbeCBind`、`TurboParser::Parser` 与
  `TurboUtils::TinyTest`，未 include、调用或复用 DataBind。
- `事实`：JSON 端到端测试使用 caller-owned CMeta native shape 和合法
  `[c(...), name(...)] type field;` grammar，覆盖 nested record、semantic/native
  rename、owning `tstr`、schema/type 非 NUL slice 在 plan 成功后覆盖并释放，以及继续
  decode。
- `事实`：borrowed JSON 测试证明 DOM 输出 `CSERDE_VIEW_STABLE` slice；reader wrapper
  先销毁，DOM/backing owner 仍存活时 `vstr` 有效，随后先执行 native
  `restore_zero`，再释放 DOM。
- `事实`：同一 JSON 集成测试文件另用受控 CSerde reader 提供 transient string
  slice，decode 返回 `CBIND_UNSUPPORTED`，destination 回到 semantic zero。
- `事实`：新增仓库内独立 install consumer。CTest 自动将 TurboParser 安装到
  `build/Msvc-Release/tbe/tbe_cbind/install_consumer/prefix`，再从独立 build tree
  `find_package(TurboParser CONFIG REQUIRED)`、编译、链接并运行真实 JSON -> C struct。
- `事实`：install consumer 的 `target_link_libraries` 只有
  `TurboParser::TbeCBind` 与 `TurboParser::Parser`；编译 include 路径来自隔离安装 prefix
  和已安装 TurboUtils，不使用源码树私有 include/target。

## RED -> GREEN

- `事实 / RED`：实现前执行
  `cmake --build --preset win-release-user --target test_tbe_cbind_json`，Ninja 报告
  `unknown target 'test_tbe_cbind_json'`，退出码 1。
- `事实 / RED`：实现前配置
  `tbe/tbe_cbind/test/install_consumer`，CMake 报告 source directory 不存在，退出码 1。
- `事实 / GREEN`：新增测试/fixture 后，专用 JSON test 与自动化 install consumer
  均单独构建、执行通过。

## DataBind 独立性证据

- `事实`：`rg.exe -n -i "databind|data_bind" tbe/tbe_cbind/include
  tbe/tbe_cbind/src` 无匹配；production TbeCBind header/source 引用数为 0。
- `事实`：安装 consumer 对 imported target interface 做递归闭包检查，得到：

  ```text
  TurboParser::TbeCBind;TurboParser::TbeSchema;TurboUtils::Core;
  TurboUtils::CMeta;TurboUtils::Platform;Threads::Threads;
  TurboUtils::Concurrency;TurboUtils::CBind;TurboUtils::CSerde;
  TurboParser::Parser
  ```

- `事实`：独立 consumer 的 MSVC verbose link command 包含 `tbe_cbind.lib`、
  `turbo_parser.lib`、`tbe_schema.lib`、`turbo_cbind.lib`、TurboUtils Core/CMeta/
  Concurrency/Platform/CSerde；不含 DataBind。检查限定在 consumer closure/link command，
  不把整个安装包 export 中定义其他独立 target 误判为依赖。

## 验证

- `cmake --fresh --preset win-release-user`：通过。
- `cmake --build --preset win-release-user --target test_tbe_cbind_json`：通过。
- `ctest --preset win-release-user -R ^test_tbe_cbind_json$ --output-on-failure`：
  1/1 通过。
- `ctest --preset win-release-user -R ^test_tbe_cbind_install_consumer$
  --output-on-failure`：1/1 通过，包含 install/configure/build/run。
- focused TbeCBind、JSON CSerde reader、TbeSchema parser/robustness 与 build-time sidecar
  相邻集合：13/13 通过。
- `cmake --build --preset win-release-user`：通过。
- `ctest --preset win-release-user --output-on-failure`：70/70 通过。
- `git diff --check`：通过。

## 自审与残余风险

- `LOW / 事实`：本任务不改 TbeCBind production target、公开 API 或 decode 实现；JSON
  依赖仅存在于测试和独立 consumer。
- `LOW / 推论`：install consumer 脚本使用 CMake/CTest 跨平台接口并处理单配置与多配置
  executable 路径，但本次只在 Windows MSVC Release 实际执行；Linux 构建仍由后续全仓
  CI 提供额外覆盖。
- `LOW / 事实`：install consumer 为保证从单个 CTest 入口可重复执行，会在串行测试中
  先增量构建主 build，再安装到受验证且位于主 build tree 内的专用目录；完整 build
  后该步骤为 no-op，当前全量 CTest 中耗时约 5.3 秒。
