# Task 5 依赖契约清理报告

日期：2026-08-24

分支：`design/cbind-runtime-schema`

基线提交：`5f6b067 fix(tbe): validate install evidence precisely`

修复提交：单个独立 cleanup fix commit；最终哈希随任务回报提供。

## 结论

- `事实`：DataBind 不是 CBind 的组成部分。本次删除了自建的 link-command / response-file
  dependency tokenizer、DataBind probe 静态库和所有 linker option 负例，不再尝试解释不同
  平台的 linker 语法，也不再对 link log 或路径文本搜索 DataBind。
- `事实`：production `TbeCBind` 的依赖契约现在在配置期精确验证：
  `LINK_LIBRARIES` 必须为 `TurboParser::TbeSchema;TurboUtils::CBind`，
  `INTERFACE_LINK_LIBRARIES` 必须为
  `$<LINK_ONLY:TurboParser::TbeSchema>;TurboUtils::CBind`。任何额外、缺失或重排依赖均
  fail fast。
- `事实`：独立安装 consumer 只采集 imported `TurboParser::TbeCBind` 自身的 raw 与
  `TARGET_GENEX_EVAL` 后 interface，分别精确匹配上述 installed contract；不枚举整个
  package 的 exported targets，因此 package 中独立存在的 `TurboParser::DataBind` 不会被
  误判为 CBind 依赖。
- `事实`：consumer 源码仍只链接 `TurboParser::TbeCBind` 和
  `TurboParser::Parser`，继续从隔离 install prefix 真实 configure、compile、link、run JSON
  到 native struct。verbose build log 保留为人工审计产物，不参与通用 linker 解析或路径
  子串判定。
- `事实`：本次未修改 production TbeCBind C header/source；既有 JSON stable DOM borrowed
  lifetime、transient rollback、path guard 和跨平台 runtime environment 分支保持不变。

## TDD 证据

- `RED / 事实`：在旧递归 DataBind-name verifier 下，将 installed interface 注入
  `$<$<CONFIG:Release>:WrapperTarget>`，且 `WrapperTarget` 间接链接非 DataBind 的
  `ExtraDependencyProbe`。运行
  `ctest --preset win-release-user -R
  '^test_tbe_cbind_install_consumer_dependency_negative$' --output-on-failure`
  时 verifier 错误接受该额外依赖，测试以
  `Installed TbeCBind accepted conditional-wrapper` 失败。这证明“只找 DataBind 名字”并不
  等价于精确 CBind 依赖契约。
- `GREEN / 事实`：改为 raw/evaluated interface 精确白名单后，同一条件包装负例与直接追加
  `DirectExtraDependency` 负例都会因
  `Installed TbeCBind interface contract mismatch` 被拒绝；正常 installed interface 证据为：

  ```text
  raw=$<LINK_ONLY:TurboParser::TbeSchema>;TurboUtils::CBind
  evaluated=TurboParser::TbeSchema;TurboUtils::CBind
  ```

- `事实`：正常 consumer 通过同一 verifier，说明 package 内独立 exported DataBind target
  不会污染 `TbeCBind` interface 判断。

## 验证

- dependency contract 两个结构负例：1/1 CTest 通过（单个 table-driven CMake test 内覆盖
  conditional-wrapper 与 direct-extra）。
- install consumer 专用集合：4/4 通过，包含 path safety、ancestor safety、dependency
  negative 与正常 installed consumer。
- JSON reader、既有 JSON CSerde、focused TbeCBind、sidecar 与 install 相邻集合：14/14
  通过。
- `cmake --build --preset win-release-user -j 4`：通过，`ninja: no work to do.`。
- `ctest --preset win-release-user --output-on-failure`：73/73 通过。
- path safety verbose：本机实际执行 `Path-safety escape case uses Windows junction` 与
  `Ancestor-link case uses Windows junction`，2/2 通过；所有 fixture、sentinel、junction
  和 backing target 均位于 main build 的专用 sandbox 内。
- `rg.exe -n -i "databind|data_bind" tbe/tbe_cbind/include
  tbe/tbe_cbind/src`：退出码 1，production header/source 0 命中。
- 旧 verifier/probe/token evidence 标识检索：退出码 1，0 命中。
- `git diff --check`：通过。

## 残余风险

- `LOW / 事实`：本机实跑环境为 Windows 11、MSVC Release、Ninja。POSIX/macOS runtime
  environment 分支本次未在对应系统动态验证，不作已验证声明。
- `LOW / 推论`：精确 interface 契约刻意把新增依赖视为显式架构变更；未来若 CBind 确需
  新依赖，应同步评审并更新 production 与 installed 两处白名单及相应负例，而不是扩展
  linker tokenizer。
