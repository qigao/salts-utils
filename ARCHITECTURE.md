# Salts 与 SaltsUtils 依赖边界

## 依赖方向

Salts 是底层能力的唯一事实源，安装并导出 Core、CSTL、CMeta、CSerde、CBind、QueryVM
以及各格式 parser。SaltsUtils 只通过已安装 Salts SDK 的公共头文件和 CMake targets 使用这些能力：

```text
application -> SaltsUtils capability -> installed Salts capability
            \-----------------------> installed Salts::CBind
```

SaltsUtils 不访问 Salts 源码目录或私有头，也不再提供聚合 `Parser` target。消费方按能力直接链接
`Salts::JsonParser`、`Salts::XmlParser`、`Salts::CmdParser` 等目标。

## 包与 targets

`find_package(SaltsUtils CONFIG REQUIRED)` 导出 SaltsUtils 所有的高层能力：

- `Salts::Cron`
- `Salts::Mustache`
- `Salts::TbeSchema`（以及 `Salts::SchemaBE` 兼容别名）
- `Salts::DataBind`、`Salts::DataBindCMeta`、`Salts::DataBindCFlow`
- `Salts::Serial`
- `Salts::Playback`
- 可选的 `Salts::Capture`、`Salts::CFlowUSB`；`Salts::LuaBind` 当前仅作为构建树内适配 target

基础 Salts 包拥有并导出：

- `Salts::Core`、`Salts::CSTL`、`Salts::CMeta`
- `Salts::CSerde`、`Salts::CBind`
- QueryVM 和各格式 parser targets

两个包共享 `Salts::` target namespace，但每个 target 只有一个所有者。SaltsUtils 的 package config
先精确加载同 profile 的 Salts package，再加载自己的 targets。

## DataBind 边界

`Salts::DataBind` 保留独立的 schema、动态值与 typed conversion 契约，并通过内部 parser
compat 层消费已安装 Salts 的格式 parser；它不是基础包 `Salts::CBind` 的 ABI 兼容别名。
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
避免 Debug/Release 或不同安装树被隐式混用。

Windows Debug/Release 分别安装到 `salts-utils/debug` 与 `salts-utils/release`，并分别使用
`salts/debug` 与 `salts/release`。构建系统不把外部 Salts DLL 复制进 SaltsUtils。

Serial 默认构建并导出 `Salts::Serial`。Windows 构建始终启用 Capture，所有标准
Windows preset 统一选择 vcpkg `capture` feature；因此常规 `install` target 会与其他库
一起导出 `Salts::Capture`，无需独立 install preset。非 Windows profile 仅在显式
启用 Capture 时导出该 target。安装态消费测试会分别验证 feature-off 不导出 Capture、
feature-on 导出 Capture，并运行 Serial/Capture 的最小 C 消费端。

CFlowUSB 默认关闭；只有同时选择 vcpkg `usb` feature 并设置
`SALTS_UTILS_ENABLE_CFLOW_USB=ON` 时才构建、安装和导出 `Salts::CFlowUSB`。libusb 保持
PRIVATE 依赖，不进入公共头或消费方链接契约。

Windows 测试进程通过 preset 的 `PATH` 查找 Salts DLL；构建系统不复制外部 DLL。
Linux preset 对应设置 `LD_LIBRARY_PATH`。

## 权衡

- 性能：DataBind range 和 publisher 不分配 payload buffer；USB 数据面使用固定容量。
- 复杂度：SaltsUtils package 边界集中导出高层 targets，底层实现仍以 Salts 为唯一事实源。
- 可维护性：parser、CMeta、CFlow 与平台能力只通过已安装 Salts 的公共 targets 消费。
- 兼容性：2.0.0 使用 `SaltsUtils` package identity 与 `Salts::` namespace；升级应安装到空
  staging prefix，避免旧 `TurboParser` 文件被增量安装残留。

## 迁移与回滚

发布前至少运行全量 CTest，并用 staging prefix 验证 Salts 与 SaltsUtils 的导出 targets。
若 package identity 迁移出现阻断，应回滚整组安装产物；不得通过重新引入 Salts 源码目录、
复制 DLL 或默认搜索路径来绕过 package 依赖。
