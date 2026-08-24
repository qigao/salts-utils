# Task 5 第二轮审查修复报告

日期：2026-08-24

分支：`design/cbind-runtime-schema`

基线提交：`934da81 fix(tbe): harden cbind install consumer checks`

修复提交：单个独立 fix commit；最终哈希随任务回报提供。

## HIGH：安全 sandbox 的 ancestor-first 验证

- `事实`：原 path-safety test 的重复运行清理会在证明 `safety_root` 及祖先可信前，
  先检查并处理深层 `previous_link`。Windows 分支还会把“整条路径的 `REAL_PATH` 与
  lexical path 不同”误当成 leaf 是 junction，因而可能对普通 leaf 调用 link-only
  removal。
- `RED / 事实`：新增 ancestor fixture 在 main build 内创建两个隔离 sandbox，以
  `main/tbe` junction 指向 backing sandbox，并在 backing 中的预期 leaf 位置放普通文件。
  旧脚本沿 ancestor junction 访问 leaf 并删除该普通文件，CTest 失败信息为
  `Path-safety script touched an ordinary file through an ancestor link`。
- `GREEN / 事实`：path-safety script 现在先规范化并验证 main，再按顺序验证
  `main/tbe`、`main/tbe/tbe_cbind` 层次对应的所有 existing components 和
  `safety_root`：
  component 必须不是 symlink/reparse point、normalized lexical path 必须等于
  `REAL_PATH`，且 canonical path 必须仍在 main build 内。任一步无法证明即以
  `untrusted ancestor` 失败，不访问任何深层 child。
- `事实`：只有 leaf 自身经 `IS_SYMLINK` 或 Windows
  `FileAttributes.ReparsePoint` 明确确认后，才允许 link-only removal；不再通过整路径
  canonical 差异推断 leaf 类型。ancestor 负例确认普通文件保持不变。
- `事实`：所有 fixture、backing target、sentinel 和 link target 都位于
  `build/Msvc-Release/tbe/tbe_cbind` 下的专用测试 sandbox，没有创建或删除 workspace
  外路径。
- `事实`：本机 `cmake -E create_symlink` 不可用，但 PowerShell junction 可用；verbose
  CTest 明确输出 `Path-safety escape case uses Windows junction` 和
  `Ancestor-link case uses Windows junction`，两个 junction 场景均实际执行而非 skip。

## MED：只检查真实依赖 token

- `事实`：旧 link verifier 虽已筛选 consumer link invocation，仍对整行和展开后的
  response text 裸搜 `DataBind`，因此 output/build/rsp 路径中的同名字样会被误判。
- `RED / 事实`：正常 installed consumer 的真实输出目录改为
  `DataBindCheckoutBuildOutput`，依赖保持不变。旧 verifier 在实际 MSVC link command 的
  `/out:` 和 `/pdb:` 路径中看到 `DataBind` 后错误失败。
- `GREEN / 事实`：verifier 使用 `separate_arguments(NATIVE_COMMAND)` tokenize 实际 link
  invocation 与其引用的 response file，只记录库依赖名：Windows `.lib`、
  `/DEFAULTLIB:`、`/WHOLEARCHIVE:`，POSIX `.a`、`.so[.version]`、`.dylib`、`.tbd`、
  `-l...` 和 framework。`-o`、`/out:`、`/implib:`、`/pdb:`、object/source、build/output
  与 rsp 路径不会参与禁止名称判定。
- `事实`：raw link/rsp 仍写入 `expanded-link-evidence.txt` 供审计；解析后的依赖 token
  单独写入 `dependency-link-tokens.txt`。正常 raw evidence 确实含
  `DataBindCheckoutBuildOutput`，但非空 dependency-token evidence 无 DataBind，consumer
  configure/build/run 通过。
- `事实`：条件负例继续强制 Ninja response file；token evidence 明确包含
  `DataBindProbe.lib <= DataBindProbe.lib`，verifier 因实际依赖名失败，不依赖路径裸搜。

## 验证

- path-safety、ancestor-safety、dependency-negative、正常 install consumer：4/4 通过。
- JSON、既有 JSON CSerde、focused TbeCBind、sidecar 与 install 相邻集合：14/14 通过。
- `cmake --build --preset win-release-user`：通过。
- `ctest --preset win-release-user --output-on-failure`：73/73 通过。
- `rg.exe -n -i "databind|data_bind" tbe/tbe_cbind/include
  tbe/tbe_cbind/src`：退出码 1，无 production header/source 引用。
- 正常 consumer `dependency-link-tokens.txt` DataBind 检索：退出码 1；负例 token 检索：
  退出码 0，命中 `DataBindProbe.lib`。
- `git diff --check`：通过。

## 残余风险

- `LOW / 事实`：本地实跑环境仍为 Windows 11、MSVC Release、Ninja；POSIX token 类型与
  loader 分支已实现但未在本机声称动态验证，仍由 Linux/macOS CI 补充。
- `LOW / 推论`：tokenizer 针对 CMake 常规产生的直接库参数、response file 和主流 linker
  选项；若未来工具链把依赖编码进非标准自定义 linker-script 参数，需要同步扩展明确的
  dependency-token 规则与负例，不能退回整行子串搜索。
