# Task 5 审查修复报告：安装路径、依赖闭包与回滚

日期：2026-08-24

分支：`design/cbind-runtime-schema`

基线提交：`7418054 Task 5: add TbeCBind JSON install consumer coverage`

修复提交：单个独立 fix commit；最终哈希随任务回报提供。

## 审查结论处理

- `HIGH / 事实`：install consumer 不再接受调用方传入的删除根目录。runner 先对
  main build 做 `REAL_PATH`，再固定推导
  `tbe/tbe_cbind/install_consumer`；路径守卫用 component-aware containment，且仅在
  Windows 大小写折叠。它拒绝 main/root equality、越界、非固定目录、symlink、junction
  和解析后不在 main build 内的现有组件，验证成功后才执行 `REMOVE_RECURSE`。
- `HIGH / 事实`：路径负例的所有外部候选和 sentinel 都位于 main build 内的专用安全
  sandbox。覆盖 `..` sibling、case-variant sibling、main equality、filesystem root 和
  reparse-point escape；本机 symlink 创建受限时成功回退到 Windows junction。链接节点
  使用 PowerShell `Remove-Item -LiteralPath` 单独移除，并确认 backing sentinel 未删除。
- `MED / 事实`：consumer 在自身 target/config 上用
  `TARGET_GENEX_EVAL` + `file(GENERATE)` 生成各 target 的 evaluated interface 证据，验证器
  仅从 `TurboParser::TbeCBind` 和 `TurboParser::Parser` 递归遍历可达闭包。因此 package
  中独立 export 的 `TurboParser::DataBind` 不会被误判。
- `MED / 事实`：条件负例向 TbeCBind 注入
  `$<$<CONFIG:Release>:WrapperTarget>`，再由 `WrapperTarget` 间接链接 `DataBindProbe`。
  evaluated closure 和 consumer 的真实链接参数都必须识别该依赖。链接验证只筛选
  consumer 实际 link invocation；若其引用 response file，则要求该文件存在并展开读取，
  不再因编译命令中的测试探针名字产生假阳性。
- `MED / 事实`：runner 按平台构造运行库环境。Windows 使用原生 `;` PATH 并加入已解析
  `TurboUtils::Core` 的 target file directory；Linux 使用 `:` PATH/`LD_LIBRARY_PATH`；
  macOS 使用 `:` PATH/`DYLD_LIBRARY_PATH`。空的继承变量不会产生尾部分隔符。
- `LOW / 事实`：transient rollback fixture 先让 owning `tstr` 从 transient token 成功复制
  并分配，再让后续 borrowed `vstr` 遇到 transient slice 返回 `CBIND_UNSUPPORTED`；测试
  断言 owning 指针和 borrowed `{data,len}` 全部恢复 semantic zero。

## RED -> GREEN

- `RED / 事实`：旧 runner 以 `${fake_main}/../outside-dotdot` 作为 `TP_TEST_ROOT` 时越过
  安全检查并删除 sibling sentinel，随后才在后续流程失败；新增 path-safety CTest 因
  `Runner followed a dotdot sandbox and deleted the outside sentinel` 失败。
- `GREEN / 事实`：固定目录推导和 real/component guard 落地后，path-safety CTest 通过；
  runner 明确拒绝任何 `TP_TEST_ROOT` override，junction escape 也在删除前拒绝。
- `RED / 事实`：旧 configure-time helper 未展开
  `$<$<CONFIG:Release>:WrapperTarget>`，dependency-negative CTest 报告未发现间接 DataBind。
- `GREEN / 事实`：生成期 evaluated interface 递归证据识别
  `TurboParser::TbeCBind -> WrapperTarget -> DataBindProbe`；强制 Ninja response file 的
  consumer link args 同样包含并识别 `DataBindProbe.lib`。
- `GREEN / 事实`：加强后的 rollback 测试在既有 core 实现上直接通过，说明该项为此前
  测试覆盖不足，而非需要 production 修复的行为缺陷。

## 独立性证据

- `事实`：`rg.exe -n -i "databind|data_bind" tbe/tbe_cbind/include
  tbe/tbe_cbind/src` 无匹配；production public header/source 引用数为 0。
- `事实`：正常 installed consumer 的 evaluated closure 是：

  ```text
  TurboParser::TbeCBind;TurboParser::TbeSchema;TurboUtils::Core;
  TurboUtils::CMeta;TurboUtils::Platform;Threads::Threads;
  TurboUtils::Concurrency;TurboUtils::CBind;TurboUtils::CSerde;
  TurboParser::Parser
  ```

- `事实`：对上述 closure 文件和筛选后的真实 consumer link evidence 执行
  `rg.exe -n -i "databind|data_bind"`，退出码为 1（无匹配）。TbeCBind 核心 target 的
  link 声明仍只有 private `TurboParser::TbeSchema` 和 public `TurboUtils::CBind`；
  Parser/JSON 依赖只位于 test/consumer。

## 验证

- `cmake --preset win-release-user`：通过。
- path-safety、dependency-negative、正常 install consumer：3/3 通过；正常 consumer
  包含隔离 install/configure/build/link-evidence/run。
- JSON、既有 JSON CSerde reader、focused TbeCBind 与 sidecar 相邻集合：13/13 通过。
- `cmake --build --preset win-release-user`：通过。
- `ctest --preset win-release-user --output-on-failure`：72/72 通过。
- `git diff --check`：通过。

## 自审与残余风险

- `LOW / 事实`：本次只在 Windows 11、MSVC Release、Ninja 上实际运行；junction 负例和
  Windows PATH/DLL 查找均已实跑。POSIX/macOS 分支按平台分别使用标准 loader 环境变量，
  但本地没有宣称 Linux 或 macOS 动态运行验证，仍需对应 CI 覆盖。
- `LOW / 推论`：路径检查与删除之间仍存在所有通用文件系统操作都可能面对的并发替换
  窗口；该 CTest sandbox 由 `RUN_SERIAL` 约束，且固定目录不接受外部输入，当前风险较低。
- `LOW / 事实`：整个 package 仍可独立导出 DataBind target；验证范围刻意限定为
  TbeCBind + Parser consumer 的可达 interface closure 与实际 link arguments。
