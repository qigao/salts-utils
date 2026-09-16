# SaltsUtils

SaltsUtils 提供基于已安装 Salts SDK 的高层能力，包括 `Salts::Crypto`、`Salts::FS`、
`Salts::Process`、QueryVM、各格式/协议 parser、Playback、Capture、Serial、Cron、Mustache、
Jinja CMeta、Unicode、TBE schema、DataBind 及构建期代码生成器。底层 Core、Platform、CFlow、
CMeta、CSTL、CSerde 与 `Salts::UriParser` 仍由 Salts 提供；SaltsUtils 不访问 Salts 源码目录或私有头。

本仓库是以下能力的唯一 owner：

- `Salts::Crypto` — RFC 8032 Ed448 与 SHA-256，公开头 `<salts/crypto.h>`；
- `Salts::FS` — bounded filesystem service、native watch 与 typed watch Publisher，公开头
  `<salts/fs.h>`、`<salts/fs_watch.h>`、`<salts/fs_watch_publisher.h>`；
- `Salts::Process` — 基于 Salts process owner 与 CFlow native pipe 的异步标准流 adapter，公开头
  `<salts/process.h>`；
- `Salts::QueryVM` 与 JSON、XML、YAML、CSV、INI、TLV/LTV、Modbus、SOA、DotEnv、Cmd、TOON、
  TOML、DateTime 等 parser component targets，以及显式的 JSON/CSerde、CYaml/JSON adapters。

Parser 仍按 component target 独立暴露，例如 `Salts::JsonParser`、`Salts::XmlParser`、
`Salts::CmdParser`；本仓库不重新引入聚合 `Parser` facade。`Salts::UriParser` 是例外：它继续由
Salts 拥有，因为 `Salts::CNet` 直接依赖这一低层 URI primitive。TLV/LTV/SOA 等 moved parser
通过已安装 Salts package 消费 `Salts::UriParser`，不会复制其源码。

旧的 `Salts::CFlowFS`、`Salts::CFlowProcess` 和 `<cflow/fs*.h>` / `<cflow/process.h>` 不提供
alias、forwarding header 或 fallback。底层 `salts_fs_*` 与 `salts_process_*` API 仍属于 Salts。
同样，迁移后的 parser/query targets 不在 Salts package 中保留 alias、重复实现或 fallback。

DataBind 由 SaltsUtils 构建、安装并导出为 `Salts::DataBind`，是 SaltsUtils 唯一的数据绑定
引擎。生成代码、现有原生 C struct 与动态对象都通过 DataBind 绑定；格式编排直接复用本包
拥有的 parser component targets，CMeta range、CFlow Stream 与 Reactive Publisher 通过独立
适配 targets 提供，不增加 DataBind 核心 target 的传递依赖。

TBE/DataBind 的规范所有权边界为：

```text
CMeta: native structure and semantic type graph       (Salts)
schema overlay: external names, presence/defaults, wire layout and validation
DataBind: native/dynamic conversion, rollback and format orchestration
CSTL: concrete container storage                      (Salts)
CSerde: canonical format-neutral token contract       (Salts)
QueryVM/parsers: format/protocol mechanics             (SaltsUtils)
UriParser: low-level URI primitive                     (Salts)
```

Generated/native and dynamic paths remain DataBind-owned over canonical CMeta structural
metadata; external names, presence/defaults, wire layout, validation, and fingerprints remain
overlay-only. Runtime-schema dynamic roots retain an immutable recursive CMeta identity graph;
their public `DataBindValueKind` remains only the storage/API discriminator and is not a second
semantic type system.

## CMake

配置时必须让 `SALTS_ROOT` 指向匹配 profile 的已安装 Salts。缺失/无效的 `SALTS_ROOT`、缺少
`Salts::Platform` / `Salts::Core` / `Salts::CFlow` / `Salts::CSTL` / `Salts::CSerde` /
`Salts::UriParser`，或 Salts 仍导出迁移前的 `Salts::Crypto` / `Salts::CFlowFS` /
`Salts::CFlowProcess` / QueryVM / moved parser targets，都会直接配置失败；不会搜索其他 prefix
或退回源码树。

```cmake
find_package(SaltsUtils CONFIG REQUIRED)

target_link_libraries(app PRIVATE
  Salts::JsonParser
  Salts::XmlParser
  Salts::Crypto
  Salts::FS
  Salts::Process
  Salts::Playback
  Salts::Mustache
  Salts::JinjaCMeta
  Salts::Unicode
  Salts::Cron
  Salts::TbeSchema
  Salts::DataBind
  Salts::DataBindCFlow)
```

`Salts::Playback` 是 Windows、Linux、macOS、Android 与 iOS 共用的有界 PCM
设备 sink；所有权、线程与背压契约见
[`docs/architecture/media-playback-ownership.md`](docs/architecture/media-playback-ownership.md)。

Mustache 与 Jinja CMeta 分别位于 [`mustache/`](mustache/) 和 [`jinja/`](jinja/)；Jinja
通过单向依赖复用 Mustache runtime，两者具有独立源码、测试、文档和安装头目录。
[`unicode/`](unicode/) 提供 re2c 生成、固定 Unicode 17.0.0 数据版本的 UTF-8 scalar 与
identifier/whitespace property API；它不包含模板引擎语义。

需要原生 C 数据绑定时消费 SaltsUtils 导出的 DataBind：

```cmake
find_package(SaltsUtils CONFIG REQUIRED)
target_link_libraries(app PRIVATE Salts::DataBind)
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
