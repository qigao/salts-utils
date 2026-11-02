# Salts 与 SaltsUtils 依赖边界

## 依赖方向

SaltsCore 是原生基础能力的唯一事实源；独立 CMeta SDK 拥有 CMeta、CSTL、CFlow、ACE
以及 Plugin。SaltsUtils 只通过两个已安装 SDK 的公共头文件和 CMake targets 使用这些能力，
同时自己拥有 QueryVM 与各格式/协议 parser：

```text
application -> SaltsUtils parser/query/utility capability -> installed Salts foundation
            \---------------------------------------------> SaltsUtils DataBind

Salts::CNet -> Salts::URI
Salts -/-> SaltsUtils
```

SaltsUtils 不访问 Salts 源码目录或私有头，也不提供聚合 `Parser` target。消费方按能力直接链接
`Salts::JsonParser`、`Salts::XmlParser`、`Salts::CmdParser` 等本包导出的 component targets。
`Salts::URI` 不在本包复制；需要 URI primitive 的 TLV/LTV/SOA 等 parser 通过已安装 SaltsCore
package 消费它。

## 包与 targets

`find_package(SaltsUtils CONFIG REQUIRED)` 导出 SaltsUtils 的高层能力，包括：

- `Salts::Crypto`（仅由独立 qigao/crypto 的已安装静态 SDK 提供，SaltsUtils 不导出）
- `CMeta::PluginABI`、`CMeta::Plugin`（由 installed CMeta SDK 提供）
- `Salts::QueryVM`
- `Salts::IniParser`
- `Salts::JsonParser`、`Salts::JsonCSerdeAdapter`
- `Salts::XmlParser`
- `Salts::CsvParser`
- `Salts::TLVParser`、`Salts::LtvParser`
- `Salts::ModbusParser`、`Salts::SoaParser`
- `Salts::DotEnvParser`、`Salts::CmdParser`
- `Salts::Toon`、`Salts::TomlParser`、`Salts::DateTimeParser`
- `Salts::CYaml`、`Salts::CYamlJsonAdapter`
- `Salts::Selector`
- `Salts::Cron`
- `Salts::Mustache`
- `Salts::IDL`（contract frontend）
- `Salts::Schema`（Data-only shape/CMeta projection）
- `Salts::DataBind`（包含格式 providers、CMeta 与 CFlow 适配）
- `Salts::BindingsCpp`、可选 `Salts::Lua` / `Salts::QuickJS`

基础 Salts 包拥有并导出：

- `Salts::Core`、`Salts::FS`、`Salts::Process`、`Salts::USB`、`Salts::Serial`、
  `Salts::Playback`、`Salts::Capture`、`Salts::Platform`
- `CMeta::CSTL`、`CMeta::CMeta`、`CMeta::CFlow`
- `Salts::CSerde`
- `Salts::URI`

`CMeta::PluginABI` / `CMeta::Plugin` 由 CMeta SDK 拥有。SaltsUtils 只消费其 manifest/export、loader/registry/lease/lifecycle 能力；Plugin 与 CFlow 无直接依赖。


`Salts::FS` 与 `Salts::Process` 是 SaltsCore 原生能力，通过 `<fs.h>` 与
`<process.h>` 使用；CFlow I/O 原语由 `CMeta::CFlow` 提供。SaltsUtils 已删除
本地 `cflow-fs` / `cflow-process` 适配层，不再构建或导出 `Salts::CFlowFS`、
`Salts::CFlowProcess` 及其旧头文件。`Salts::USB` 已完整迁入 SaltsCore，
SaltsUtils 不再保留第二份 `cflow_usb_*` 源码或 target。
`Salts::Crypto` 同样由 SaltsCore SDK 的静态 target 提供。

QueryVM 与 moved parser targets 只由 SaltsUtils 导出。SaltsCore 使用 `Salts::`
namespace，CMeta 使用 `CMeta::` namespace；SaltsUtils 的适配器和高层能力继续使用
`Salts::`，每个 target 只有一个 owner。Package config 先精确加载同 profile 的
SaltsCore 与 CMeta，再加载自己的 targets，不搜索旧单体 package 或源码 fallback。

## Parser 与 CSerde 边界

格式 parser 与 CSerde composition 仍保持显式：

```text
Salts::JsonParser -> Salts::Core + Salts::QueryVM + CMeta::CSTL
Salts::JsonCSerdeAdapter -> Salts::JsonParser + installed Salts::CSerde

Salts::TLVParser / LtvParser / SoaParser -> installed Salts::URI
```

`Salts::JsonParser` 不因为存在 CSerde adapter 就新增 CSerde 依赖。UriParser 也不因上层 parser
使用而反向依赖 SaltsUtils。这样 package graph 始终保持 `SaltsUtils -> Salts` 单向。

## IDL / Schema / DataBind 边界

4.1 明确三个不同事实源：

```text
Salts::IDL
  syntax / contract frontend / Service / Channel / Component
        |
        v
canonical contract model

Salts::Schema
  Data-only message / enum / union shape
  external names / presence / defaults / validation
  logical data -> CMeta projection

Salts::DataBind
  CSerde + CMeta <-> native storage
  BindingPlan / ValidationPlan
  lifecycle / bounds / rollback
```

`Schema ⊂ IDL`，但 `Schema` 不拥有 Service/Channel/Component。production
`Salts::DataBind` 不链接 IDL frontend 或 parser；compiler/tooling 私有消费
`Salts::IDL`，native runtime 只消费编译后的 plan 与 canonical CMeta/CSerde。

TBE 是 format/backend，不是 IDL 本体。TBE field-order、wire-size、cursor 等
representation policy 必须逐步从 frontend parser 下沉到 format compiler，不能成为
IDL semantic truth。

DataBind 编译工具的源码统一归入 `databind/compiler/`：根目录拥有 CLI、projection、
模板与生成测试，`cmeta/` 接收外部反射输入，`binding/` 负责 native binding lowering，
`binary/` 负责 wire lowering。IDL 前端留在 `databind/idl/`，Binary wire 公共头留在
`databind/formats/binary/`。这些能力保留独立 target；IDE 中统一显示于
`databind/compiler/{target}`。相比分散目录，工具源码有统一入口；相比分合并为一个库，
独立 CMeta producer 不增加 IDL 依赖，运行时的既有链接边界也不改变。

编译任务与模板渲染的内存由各自的 CMeta lifecycle/scope 独占，body 的提前返回统一
触发恢复到零状态。Contract 是语义事实源，render tree 与 Binary plan 为派生数据，
只在当前同步、单线程调用内使用；scope 不引入额外堆分配、缓存或容量增长。
文件流与临时文件的关闭、删除、rename 属于可失败操作，必须显式检查后再离开 scope；
无失败析构只释放内存。单个输出仍在完整写入并关闭临时文件后发布，失败保留旧输出。
多输出编译沿用逐文件发布契约，不增加跨文件事务保证。

迁移只更新源码引用、文档链接与正式测试路径；target、安装头、CLI 参数和生成格式
保持兼容。回滚时可按原职责迁回目录并恢复 CMake 路径，无需迁移用户数据。
验证范围包括编译失败后重试、输出替换失败、生成代码生命周期以及 CMeta producer。

## 所有权与行为

Parser capability migration 只改变 repository/package ownership，不改变 parser state machine、
输入输出语义、公开 C headers、diagnostics、error codes 或 runtime ownership contract。

本次 package 迁移不改变 Cron、Mustache 运行时所有权契约。Serial、Playback 与 Capture
已经迁入 SaltsCore；SaltsUtils 不再编译、安装或导出这些能力的第二份实现。
Capture frame 仍是仅在同步 callback 返回前有效的 borrowed view；Serial handle 仍拥有 RX/TX
SPSC buffer，producer/consumer 拓扑与可用容量 `configured_size - 1` 不变。

Playback 与 Capture 同属 SaltsCore Media I/O 边界。`Salts::Playback` 只拥有 native
output device 与有界 PCM SPSC ring；文件 demux、decode、clock 和 playlist 由媒体层拥有。
PCM 写入在返回前完成复制，短写是显式背压，destroy 在释放前同步静止 native callback。

USB 的固定 transfer slots、bounded hotplug queue、borrowed transfer buffer、exactly-once
terminal completion 与 quiescent destroy 契约由 SaltsCore `Salts::USB` 维护。

`Salts::IDL`、`Salts::Schema`、DataBind 与当前 compiler tooling 位于 SaltsUtils；TBE 仅是 format/backend。编译器只在构建、CI 和代码生成阶段运行；数据库
DDL 生成不会成为部署后二进制的运行时依赖。

## 构建与发布

配置时必须设置与 profile 对应的 `SALTSCORE_ROOT` 和 `CMETA_ROOT`。根构建和安装后的
`SaltsUtilsConfig.cmake` 分别只在这两个根下执行 `find_package(SaltsCore 0.1 ...)` 与
`find_package(CMeta 0.1 ...)`，避免旧单体 SDK 或其他安装树被隐式选中。

Windows Debug/Release 分别安装到 `salts-utils/debug` 与 `salts-utils/release`，并消费
对应 profile 的 SaltsCore 与 CMeta SDK。构建系统不把外部 SDK DLL 复制进 SaltsUtils，
也不再提供 Capture 专用 feature、preset 或 install graph。

Windows 测试进程通过 preset 的 `PATH` 查找 Salts DLL；构建系统不复制外部 DLL。
Linux preset 对应设置 `LD_LIBRARY_PATH`。

仓库不维护通过“先 staging install，再配置一个 synthetic consumer”实现的 CMake
install-verification framework。验证只通过正常 build 与正常 CTest graph；普通安装、package
config 生成和 export set 仍是产品能力。

## 权衡

- 性能：parser runtime 行为未改变；DataBind range/publisher 不分配 payload buffer；USB 数据面使用固定容量。
- 复杂度：Salts 保持 foundation package；SaltsUtils 统一拥有 parser/query 与 higher-level utilities，
  避免低层 Salts 因 format capability 继续膨胀。
- 可维护性：每个 `Salts::` target 只有一个 owner；跨 repo 仅通过 installed Salts targets 连接。
- 兼容性：这是明确的 package ownership breaking cutover。target 名与公开 parser C API 保持稳定，
  但 consumer 必须从 `SaltsUtils` package 获取 moved parser/query targets；不提供 alias、forwarding
  header、duplicate source 或 fallback。

## 迁移与回滚

迁移顺序为：SaltsUtils 建立新的 parser/query owner 与 old-owner fail-fast；Salts 将 UriParser
独立为低层模块并删除其余 parser/query owner；CI pin 到 exact Salts cutover；两个仓库分别通过
正常 build/CTest/package export 验证后再合并。

发布前确认 SaltsCore、CMeta 与 SaltsUtils 各自只导出所属 targets，并且 source/build graph
不包含旧 owner、source-tree fallback 或 install-verification hook。
若迁移出现阻断，应回滚整组产物；不得通过重新引入 Salts parser 源码、复制实现、默认搜索路径
或 compatibility alias 绕过 package 依赖。
