# TurboParser / TurboUtils Parser 依赖边界

## 背景

低层 parser 已迁入 TurboUtils 的 `parser/` 子树，并作为可安装组件导出。
TurboParser 保留统一 facade、Mustache、Cron、TBE、DataBind、代码生成器与私有
`vendor/cxml`。TurboParser 只消费已安装的 TurboUtils 公共头和 CMake targets，
不访问 TurboUtils parser 的源码目录或私有头。

## 选择

依赖方向固定为：

```text
application -> TurboParser facade -> installed TurboUtils parser targets
                             \-----> TurboUtils::Core
```

TurboParser 拥有并导出：

- `TurboParser::Parser`
- `TurboParser::Cron`
- `TurboParser::Mustache`
- `TurboParser::TbeSchema`（兼容别名 `TurboParser::SchemaBE`）
- `TurboParser::DataBind`
- `TurboParser::Serial`
- 可选的 `TurboParser::Capture`

TurboUtils 拥有并安装 QueryVM、JSON、YAML、CSV、INI、URI、TLV/LTV、Modbus、
SOA、DotEnv、Cmd、TOON、TOML、DateTime 与 Selector 等低层 parser targets；
其 namespace 统一为 `TurboUtils::*`。CYAML/TOON 的 JSON adapter 随对应 parser 安装。
第三方 cxml 与 Monocypher 由 TurboParser 私有持有，不向使用者暴露其 target 或生命周期。

设备采集与串口实现也由 TurboParser 单独持有。迁移只改变源码和 CMake target 的归属：
`turbo_capture.h`、`turbo_serial.h`、动态库文件名、C ABI、错误码和对象生命周期不变。
Capture frame 仍是仅在同步 callback 返回前有效的 borrowed view；Serial handle 仍拥有
RX/TX SPSC buffer，producer/consumer 拓扑与可用容量 `configured_size - 1` 不变。

TBE schema 与 DataBind 属于运行时层；`tbe_compiler` 只在构建、CI 和代码生成阶段运行，
不会被 DataBind 在运行时调用。DataBind 的公共头文件包含 `turbo_parser.h`，因此
`TurboParser::Parser` 是其 PUBLIC 依赖；schema 与 Mustache 仅为实现或编译器依赖。
依赖 CmdParser、cxml 与 Mustache 的 `junit_to_html` 也归 TurboParser 所有；它是构建树
工具，不构成 TurboUtils 或 TurboParser 的安装时库依赖。
依赖 CSV parser 的 SQLite VDBE benchmark 随 CSV 模块维护，避免 TurboUtils 测试目标
携带 parser 链接项。

## 候选方案

1. 解析器由 TurboParser 源码树私有持有：facade 易于构建，但其他消费者无法通过
   已安装 SDK 复用格式解析器，并会诱发跨仓源码 include。
2. TurboParser 复制 TurboUtils 基础实现：构建独立，但产生错误码、容器和正则的
   双重事实源，修复和 ABI 容易分叉。
3. TurboUtils 安装 parser，TurboParser 作为 facade 单向消费：增加 TurboUtils SDK
   的组件数量，但形成唯一实现与可独立复用的公共边界。本仓库采用此方案。

## 接口、所有权与错误语义

Parser 自有的 DOM、SAX、QueryVM、数据格式、对象所有权与错误码保持不变，解析结果仍由
对应的 `turbo_free_*`/`*_free` 接口释放。此次边界调整有意移除跨包 JSON token reader、
native binding bridge 及其生成器选项；现有消费者必须在升级前把这类适配放到应用层或
独立桥接包。TurboUtils 类型只在剩余公共契约中出现，不将其内部源码头文件暴露给使用者。

JSONPath contains 扫描通过已安装的 `turbo_simd_scan.h` 调用
`turbo_scan_mem()`；TurboParser 不再包含 TurboUtils 私有 `re_scan.h`。输入仍是显式
长度的只读字节视图，返回值仍只表达是否找到子串。

## 构建与发布

本次删除跨包 reader、binding bridge、相关 target 与生成器选项属于公开 package API
破坏性变更，因此 TurboParser 发布版本升为 2.0.0，package version compatibility 限制为
同一 major version。major 升级必须安装到空 staging prefix，再以原子目录替换或由包管理器
卸载旧版本；CMake 的增量 install 不会删除旧版本留下的头文件、库或模板。

配置阶段执行 `find_package(TurboUtils CONFIG REQUIRED)`，缺少
`TurboUtils::Core` 时立即失败。启用测试时还要求 `TurboUtils::TinyTest`。安装包通过
`TurboParserConfig.cmake` 调用 `find_dependency(TurboUtils CONFIG)`，因此消费方只需把
两个安装前缀加入 `CMAKE_PREFIX_PATH`。

Serial 默认构建并导出 `TurboParser::Serial`。Capture 默认关闭；启用时同时设置
`TURBO_ENABLE_CAPTURE=ON` 与 vcpkg `capture` feature，安装包才导出
`TurboParser::Capture`。安装态消费测试会分别验证 feature-off 不导出 Capture、
feature-on 导出 Capture，并运行 Serial/Capture 的最小 C 消费端。

Windows 测试进程通过 preset 的 `PATH` 查找 TurboUtils DLL；构建系统不复制外部 DLL。
Linux preset 对应设置 `LD_LIBRARY_PATH`。

## 权衡

- 性能：公共 SIMD 扫描 API 保留原有向量化路径；未增加每次匹配的虚调用或分配。
- 复杂度：新增一个 CMake package 边界，但移除了缺失子目录和重复 vendor target。
- 可维护性：TurboUtils API 是基础能力的唯一事实源；解析器 target 使用独立命名空间。
- 兼容性：Parser 自有 C API 保持稳定；被移除的跨包 reader、bridge target 和生成器选项
  不提供兼容别名。CMake 使用者还需把 Parser、Cron、Mustache、TBE 和 DataBind
  target 从 `TurboUtils::*` 迁移到 `TurboParser::*`，并改为
  `find_package(TurboParser CONFIG REQUIRED)`。Capture 与 Serial 使用者分别从
  `TurboUtils::Capture`、`TurboUtils::turbo_serial` 迁移到
  `TurboParser::Capture`、`TurboParser::Serial`。

## 迁移与回滚

迁移顺序为：先把跨包适配迁出 TurboParser 消费边界，再发布包含 Capture/Serial 的
TurboParser，然后迁移消费方的 CMake target，最后发布移除旧 targets 的 TurboUtils。
发布前至少运行全量 CTest，并用 staging prefix 检查两个包的导出 target。

若独立发布出现阻断，在 TurboUtils 清理版本发布前可回滚消费方的 CMake target；数据和
C API 无需迁移。TurboUtils 清理版本发布后应回滚整组发布，而不是让两个仓库长期同时
导出同一实现。不得通过重新引入 TurboUtils 源码目录或复制 DLL 的方式绕过 package 依赖。
