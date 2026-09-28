# Salts 与 SaltsUtils 依赖边界

## 依赖方向

Salts 是基础能力的唯一事实源，安装并导出 Core、CSTL、CMeta、CSerde、CFlow、Platform
以及低层 `Salts::UriParser`。SaltsUtils 只通过已安装 Salts SDK 的公共头文件和 CMake targets
使用这些基础能力，同时自己拥有 QueryVM 与各格式/协议 parser：

```text
application -> SaltsUtils parser/query/utility capability -> installed Salts foundation
            \---------------------------------------------> SaltsUtils DataBind

Salts::CNet -> Salts::UriParser
Salts -/-> SaltsUtils
```

SaltsUtils 不访问 Salts 源码目录或私有头，也不提供聚合 `Parser` target。消费方按能力直接链接
`Salts::JsonParser`、`Salts::XmlParser`、`Salts::CmdParser` 等本包导出的 component targets。
`Salts::UriParser` 不在本包复制；需要 URI primitive 的 TLV/LTV/SOA 等 parser 通过已安装 Salts
package 消费它。

## 包与 targets

`find_package(SaltsUtils CONFIG REQUIRED)` 导出 SaltsUtils 的高层能力，包括：

- `Salts::Crypto`
- `Salts::PluginABI`、`Salts::Plugin`（由 installed Salts 1.8 提供）
- `Salts::FS`
- `Salts::Process`
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
- `Salts::DataBind`、`Salts::DataBindCMeta`、`Salts::DataBindCFlow`
- `Salts::BindingsCpp`、可选 `Salts::Lua` / `Salts::QuickJS`
- `Salts::Serial`
- `Salts::Playback`
- 可选的 `Salts::Capture`、`Salts::CFlowUSB`

基础 Salts 包拥有并导出：

- `Salts::Core`、`Salts::CSTL`、`Salts::CMeta`
- `Salts::CFlow`、`Salts::Platform`
- `Salts::CSerde`
- `Salts::UriParser`

`Salts::PluginABI` / `Salts::Plugin` 由 Salts 1.8 拥有。SaltsUtils 只消费其 manifest/export、loader/registry/lease/lifecycle 能力；Plugin 与 CFlow 无直接依赖，历史 `PluginCFlow` public subsystem 在 4.1 中删除。


`Salts::FS` 组合 Salts 的同步文件系统 API 与 CFlow bounded execution；`Salts::Process`
组合 Salts 的 process owner 与 CFlow native byte-pipe execution。`Salts::Crypto` 依赖
`Salts::Platform` 的系统 CSPRNG。三个 target 均只由 SaltsUtils 导出；旧的
`Salts::CFlowFS` 与 `Salts::CFlowProcess` 不提供兼容 alias。

QueryVM 与 moved parser targets 也只由 SaltsUtils 导出。两个包共享 `Salts::` target namespace，
但每个 target 只有一个 owner。SaltsUtils 的 package config 先精确加载同 profile 的 Salts package，
再加载自己的 targets。如果所选 Salts package 仍导出 `Salts::Crypto`、`Salts::CFlowFS`、
`Salts::CFlowProcess`、`Salts::QueryVM` 或任何 moved parser target，配置立即失败，不尝试 fallback；
`Salts::UriParser` 则必须存在于 Salts package。

## Parser 与 CSerde 边界

格式 parser 与 CSerde composition 仍保持显式：

```text
Salts::JsonParser -> Salts::Core + Salts::QueryVM + Salts::CSTL
Salts::JsonCSerdeAdapter -> Salts::JsonParser + installed Salts::CSerde

Salts::TLVParser / LtvParser / SoaParser -> installed Salts::UriParser
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

## 所有权与行为

Parser capability migration 只改变 repository/package ownership，不改变 parser state machine、
输入输出语义、公开 C headers、diagnostics、error codes 或 runtime ownership contract。

本次 package 迁移也不改变 Cron、Mustache、Serial 和 Capture 运行时所有权契约。
Capture frame 仍是仅在同步 callback 返回前有效的 borrowed view；Serial handle 仍拥有 RX/TX
SPSC buffer，producer/consumer 拓扑与可用容量 `configured_size - 1` 不变。

Playback 与 Capture 同属 SaltsUtils Media I/O 边界。`Salts::Playback` 只拥有 native
output device 与有界 PCM SPSC ring；文件 demux、decode、clock 和 playlist 由媒体层拥有。
PCM 写入在返回前完成复制，短写是显式背压，destroy 在释放前同步静止 native callback。

CFlowUSB 由一个内部线程独占 libusb native events，使用固定 transfer slots 与 bounded hotplug
queue，并只由 `cflow_usb_run_ready()` 交付用户 callback。borrowed transfer buffer、exactly-once
terminal completion 与 quiescent destroy 是其公开生命周期契约。

`Salts::IDL`、`Salts::Schema`、DataBind 与当前 compiler tooling 位于 SaltsUtils；TBE 仅是 format/backend。编译器只在构建、CI 和代码生成阶段运行；数据库
DDL 生成不会成为部署后二进制的运行时依赖。

## 构建与发布

配置时必须设置与 profile 对应的 `SALTS_ROOT`。根构建和安装后的
`SaltsUtilsConfig.cmake` 都只在该根下执行 `find_package(Salts CONFIG REQUIRED ... NO_DEFAULT_PATH)`，
避免 Debug/Release 或不同安装树被隐式混用。缺失/无效的 `SALTS_ROOT`，或缺失
`Salts::Platform` / `Salts::Core` / `Salts::CFlow` / `Salts::CSTL` / `Salts::CSerde` /
`Salts::UriParser` 都直接 `FATAL_ERROR`。

同样，如果 Salts package 仍导出已迁移 owner targets（Crypto、旧 CFlowFS/Process、QueryVM 或
任一 moved parser），SaltsUtils 立即 fail-fast，避免同一个 `Salts::` target 出现两个 owner。

Windows Debug/Release 分别安装到 `salts-utils/debug` 与 `salts-utils/release`，并分别使用
`salts/debug` 与 `salts/release`。构建系统不把外部 Salts DLL 复制进 SaltsUtils。

Serial 默认构建并导出 `Salts::Serial`。Windows 构建始终启用 Capture，所有标准
Windows preset 统一选择 vcpkg `capture` feature；因此常规 `install` target 会与其他库
一起导出 `Salts::Capture`，无需独立 install preset。非 Windows profile 仅在显式
启用 Capture 时导出该 target。

CFlowUSB 默认关闭；只有同时选择 vcpkg `usb` feature 并设置
`SALTS_UTILS_ENABLE_CFLOW_USB=ON` 时才构建、安装和导出 `Salts::CFlowUSB`。libusb 保持
PRIVATE 依赖，不进入公共头或消费方链接契约。

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

发布前确认 Salts package 仅保留 `Salts::UriParser`，SaltsUtils package 拥有 moved parser/query
目标，并且 source/build graph 不包含旧 owner、source-tree fallback 或 install-verification hook。
若迁移出现阻断，应回滚整组产物；不得通过重新引入 Salts parser 源码、复制实现、默认搜索路径
或 compatibility alias 绕过 package 依赖。
