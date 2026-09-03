# SaltsUtils

SaltsUtils 提供基于 Salts 的高层工具，包括 Mustache、Cron、TBE schema、Serial、Capture
及构建期代码生成器。底层 JSON、XML、YAML、CSV、Cmd 等 parser 由 Salts 直接提供；
SaltsUtils 不再提供聚合 parser facade。

DataBind runtime、公共头和 CMake target 已退出默认构建、安装与 export。遗留源码和旧生成模板
暂时保留供迁移参考；新代码应使用 Salts 基础包的 `Salts::CBind` 与 `<cbind/cbind.h>`。
SaltsUtils 不提供 `Salts::DataBind` 兼容 target，旧模板也不应再用于新项目。

## CMake

设置与当前构建 profile 对应的 `SALTS_ROOT`，然后使用：

```cmake
find_package(SaltsUtils CONFIG REQUIRED)

target_link_libraries(app PRIVATE
  Salts::Mustache
  Salts::Cron
  Salts::TbeSchema)
```

需要原生 C 数据绑定时直接消费基础 Salts：

```cmake
find_package(Salts CONFIG REQUIRED)
target_link_libraries(app PRIVATE Salts::CBind)
```

## TBE Compiler

- 编译器选项与可运行示例：[`tbe/tbe_compiler/CLI_OPTIONS.md`](tbe/tbe_compiler/CLI_OPTIONS.md)
- 数据库 DDL 设计与边界：[`docs/architecture/tbe-database-ddl-generation.md`](docs/architecture/tbe-database-ddl-generation.md)
- 遗留 DataBind 迁移说明：[`tbe/data_bind/README.md`](tbe/data_bind/README.md)

数据库语言生成空库初始化所需的表、外键、普通/唯一复合索引、自定义 `CHECK` 与种子 `INSERT`。
它不是 migration engine，不会比较线上结构、生成 `ALTER TABLE` 或连接数据库执行迁移。

SQLite `uint64` 以受严格约束的 canonical decimal `TEXT` 保存，避免大整数经 REAL 静默舍入；
PostgreSQL 字符串默认值使用与 `standard_conforming_strings` 无关的 escape literal。identity 能力按
dialect 校验：SQLite 仅接受有符号整数 rowid，PostgreSQL 接受 `uint8`/`uint16`/`uint32` 并保留值域
约束，但拒绝无法由 sequence 精确承载的 `uint64`。完整契约见上述数据库 DDL 设计文档。
