# SaltsUtils

SaltsUtils 提供基于 Salts 的高层工具，包括相互独立的 Mustache、Jinja CMeta 与 Unicode
模块、Cron、TBE schema、DataBind、Serial、Capture、Playback 及构建期代码生成器。底层 JSON、XML、
YAML、CSV、Cmd 等 parser 由 Salts 直接提供；SaltsUtils 不再提供聚合 parser facade。

DataBind 保留独立的 schema 驱动动态值与 typed conversion API，并通过 Salts parser 实现格式
适配；它不是基础包 `Salts::CBind` 的 ABI 兼容别名。CMeta range、CFlow Stream 与 Reactive
Publisher 通过独立适配 targets 提供，不增加 DataBind 核心 target 的传递依赖。

## CMake

设置与当前构建 profile 对应的 `SALTS_ROOT`，然后使用：

```cmake
find_package(SaltsUtils CONFIG REQUIRED)

target_link_libraries(app PRIVATE
  Salts::Playback
  Salts::Mustache
  Salts::JinjaCMeta
  Salts::Unicode
  Salts::Cron
  Salts::TbeSchema
  Salts::DataBindCFlow)
```

`Salts::Playback` 是 Windows、Linux、macOS、Android 与 iOS 共用的有界 PCM
设备 sink；所有权、线程与背压契约见
[`docs/architecture/media-playback-ownership.md`](docs/architecture/media-playback-ownership.md)。

Mustache 与 Jinja CMeta 分别位于 [`mustache/`](mustache/) 和 [`jinja/`](jinja/)；Jinja
通过单向依赖复用 Mustache runtime，两者具有独立源码、测试、文档和安装头目录。
[`unicode/`](unicode/) 提供 re2c 生成、固定 Unicode 17.0.0 数据版本的 UTF-8 scalar 与
identifier/whitespace property API；它不包含模板引擎语义。

需要原生 C 数据绑定时直接消费基础 Salts：

```cmake
find_package(Salts CONFIG REQUIRED)
target_link_libraries(app PRIVATE Salts::CBind)
```

## TBE Compiler

- 编译器选项与可运行示例：[`tbe/tbe_compiler/CLI_OPTIONS.md`](tbe/tbe_compiler/CLI_OPTIONS.md)
- 数据库 DDL 设计与边界：[`docs/architecture/tbe-database-ddl-generation.md`](docs/architecture/tbe-database-ddl-generation.md)
- DataBind 设计、所有权与适配器说明：[`tbe/data_bind/README.md`](tbe/data_bind/README.md)

数据库语言生成空库初始化所需的表、外键、普通/唯一复合索引、自定义 `CHECK` 与种子 `INSERT`。
它不是 migration engine，不会比较线上结构、生成 `ALTER TABLE` 或连接数据库执行迁移。

SQLite `uint64` 以受严格约束的 canonical decimal `TEXT` 保存，避免大整数经 REAL 静默舍入；
PostgreSQL 字符串默认值使用与 `standard_conforming_strings` 无关的 escape literal。identity 能力按
dialect 校验：SQLite 仅接受有符号整数 rowid，PostgreSQL 接受 `uint8`/`uint16`/`uint32` 并保留值域
约束，但拒绝无法由 sequence 精确承载的 `uint64`。完整契约见上述数据库 DDL 设计文档。
