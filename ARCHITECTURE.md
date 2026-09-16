# Salts 与 SaltsUtils 依赖边界

## 依赖方向

Salts 是底层能力的唯一事实源，安装并导出 Core、CSTL、CMeta、CSerde、QueryVM
以及各格式 parser。SaltsUtils 只通过已安装 Salts SDK 的公共头文件和 CMake targets 使用这些能力：

```text
application -> SaltsUtils capability -> installed Salts capability
            \-----------------------> installed Salts::DataBind
```

SaltsUtils 不访问 Salts 源码目录或私有头，也不再提供聚合 `Parser` target。消费方按能力直接链接
`Salts::JsonParser`、`Salts::XmlParser`、`Salts::CmdParser` 等目标。

## 包与 targets

`find_package(SaltsUtils CONFIG REQUIRED)` 导出 SaltsUtils 所有的高层能力：

- `Salts::Crypto`
- `Salts::FS`
- `Salts::Process`
- `Salts::Cron`
- `Salts::Mustache`
- `Salts::TbeSchema`（以及 `Salts::SchemaBE` 兼容别名）
- `Salts::DataBind`、`Salts::DataBindCMeta`、`Salts::DataBindCFlow`
- `Salts::Serial`
- `Salts::Playback`
- 可选的 `Salts::Capture`、`Salts::CFlowUSB`；`Salts::LuaBind` 当前仅作为构建树内适配 target

基础 Salts 包拥有并导出：

- `Salts::Core`、`Salts::CSTL`、`Salts::CMeta`
- `Salts::CFlow`、`Salts::Platform`
- `Salts::CSerde`
- QueryVM 和各格式 parser targets

`Salts::FS` 组合 Salts 的同步文件系统 API 与 CFlow bounded execution；`Salts::Process`
组合 Salts 的 process owner 与 CFlow native byte-pipe execution。`Salts::Crypto` 依赖
`Salts::Platform` 的系统 CSPRNG。三个 target 均只由 SaltsUtils 导出；旧的
`Salts::CFlowFS` 与 `Salts::CFlowProcess` 不提供兼容 alias。

两个包共享 `Salts::` target namespace，但每个 target 只有一个所有者。SaltsUtils 的 package config
先精确加载同 profile 的 Salts package，再加载自己的 targets。如果所选 Salts package 仍导出
`Salts::Crypto`、`Salts::CFlowFS` 或 `Salts::CFlowProcess`，配置立即失败，不尝试 fallback。

## DataBind 边界

DataBind 由 SaltsUtils 构建、安装并导出为 `Salts::DataBind`，是 SaltsUtils 唯一的数据绑定
引擎。生成代码、现有原生 C struct 与动态对象的转换与失败回滚都由 DataBind 执行；它直接消费
已安装 Salts 的格式 parser，不提供其他 binder、fallback 或 compatibility route。

规范所有权边界为：

```text
CMeta: native structure and semantic type graph
schema overlay: external names, presence/defaults, wire layout and validation
DataBind: native/dynamic conversion, rollback and format orchestration
CSTL: concrete container storage
CSerde/parsers: format tokens and mechanics
```

Generated/native and dynamic paths remain DataBind-owned over canonical CMeta structural
metadata; external names, presence/defaults, wire layout, validation, and fingerprints remain
overlay-only.

`Salts::DataBindCMeta` 将不可变 DataBind 容器暴露为 borrowed `cmeta_range`，
`Salts::DataBindCFlow` 在其上提供同步 Stream 与 Reactive Publisher。适配库不改变 DataBind
核心 ABI，也不拥有或缓存 payload；调用方必须让 DataBind owner 存活至遍历或 subscription
关闭。

## 所有权与行为

本次 package 迁移不改变仍保留的 Cron、Mustache、Serial 和 Capture 运行时所有权契约。
Capture frame 仍是仅在同步 callback 返回前有效的 borrowed view；Serial handle 仍拥有 RX/TX
SPSC buffer，producer/consumer 拓扑与可用容量 `configured_size - 1` 不变。

Playback 与 Capture 同属 SaltsUtils Media I/O 边界。`Salts::Playback` 只拥有 native
output device 与有界 PCM SPSC ring；文件 demux、decode、clock 和 playlist 由媒体层拥有。
PCM 写入在返回前完成复制，短写是显式背压，destroy 在释放前同步静止 native callback。

CFlowUSB 由一个内部线程独占 libusb native events，使用固定 transfer slots 与 bounded hotplug
queue，并只由 `cflow_usb_run_ready()` 交付用户 callback。borrowed transfer buffer、exactly-once
terminal completion 与 quiescent destroy 是其公开生命周期契约。

TBE schema 与 `tbe_compiler` 位于 SaltsUtils。编译器只在构建、CI 和代码生成阶段运行；数据库
DDL 生成不会成为部署后二进制的运行时依赖。

## 构建与发布

配置时必须设置与 profile 对应的 `SALTS_ROOT`。根构建和安装后的
`SaltsUtilsConfig.cmake` 都只在该根下执行 `find_package(Salts CONFIG REQUIRED ... NO_DEFAULT_PATH)`，
避免 Debug/Release 或不同安装树被隐式混用。缺失/无效的 `SALTS_ROOT`、缺失的
`Salts::Platform`/`Salts::Core`/`Salts::CFlow` 或旧 owner 冲突都直接 `FATAL_ERROR`。

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

- 性能：DataBind range 和 publisher 不分配 payload buffer；USB 数据面使用固定容量。
- 复杂度：SaltsUtils package 边界集中导出高层 targets，底层实现仍以 Salts 为唯一事实源。
- 可维护性：parser、CMeta、CFlow 与平台能力只通过已安装 Salts 的公共 targets 消费。
- 兼容性：2.0.0 使用 `SaltsUtils` package identity 与 `Salts::` namespace；迁移后的
  FS/Process target 与头路径是明确 breaking cutover，不提供 alias、forwarding header 或 fallback。

## 迁移与回滚

发布前运行正常全量 CTest，并确认 source/build graph 不包含旧 target、旧公开头路径或
install-verification hook。若 package identity 迁移出现阻断，应回滚整组产物；不得通过重新引入
Salts 源码目录、复制 DLL、默认搜索路径或 compatibility alias 绕过 package 依赖。
