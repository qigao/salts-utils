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
- `Salts::Serial`
- 可选的 `Salts::Capture`；`Salts::LuaBind` 当前仅作为构建树内适配 target

基础 Salts 包拥有并导出：

- `Salts::Core`、`Salts::CSTL`、`Salts::CMeta`
- `Salts::CSerde`、`Salts::CBind`
- QueryVM 和各格式 parser targets

两个包共享 `Salts::` target namespace，但每个 target 只有一个所有者。SaltsUtils 的 package config
先精确加载同 profile 的 Salts package，再加载自己的 targets。

## DataBind 退场

旧 DataBind 不是 CBind 的 ABI 兼容别名，不能通过名称替换安全迁移。它的 runtime、公共头和
CMake target 已从 SaltsUtils 的默认构建、测试、安装及 export 中移除；`tbe/data_bind/`
暂时只保留为迁移参考，且不会创建或安装 `Salts::DataBind`。旧的 typed/Lua 生成模板属于
迁移期遗留接口，新功能不再基于它们扩展。

新代码直接链接基础 Salts 的 `Salts::CBind`，使用 CMeta 数据描述符和 CSerde reader/writer 完成
格式无关绑定。具体 JSON/XML/YAML 等格式适配器仍由对应 Salts parser 层提供，不由 SaltsUtils
重新包装。

## 所有权与行为

本次 package 迁移不改变仍保留的 Cron、Mustache、Serial 和 Capture 运行时所有权契约。
Capture frame 仍是仅在同步 callback 返回前有效的 borrowed view；Serial handle 仍拥有 RX/TX
SPSC buffer，producer/consumer 拓扑与可用容量 `configured_size - 1` 不变。

TBE schema 与 `tbe_compiler` 位于 SaltsUtils。编译器只在构建、CI 和代码生成阶段运行；数据库
DDL 生成不会成为部署后二进制的运行时依赖。

## 构建与发布

配置时必须设置与 profile 对应的 `SALTS_ROOT`。根构建和安装后的
`SaltsUtilsConfig.cmake` 都只在该根下执行 `find_package(Salts CONFIG REQUIRED ... NO_DEFAULT_PATH)`，
避免 Debug/Release 或不同安装树被隐式混用。

Windows Debug/Release 分别安装到 `salts-utils/debug` 与 `salts-utils/release`，并分别使用
`salts/debug` 与 `salts/release`。构建系统不把外部 Salts DLL 复制进 SaltsUtils。

本次删除旧 parser facade、DataBind export 和旧 package identity 是破坏性 package API 变更，
因此版本为 2.0.0。升级应安装到空 staging prefix；增量 install 不会删除旧版本残留的头文件、
库或模板。
