# TurboParser

TurboParser 提供统一 parser facade、Mustache、Cron、TBE schema、DataBind 与构建期代码生成工具。
运行时库与构建期工具保持分层：`tbe_compiler` 用于生成代码、RulesForge DSL 和数据库 bootstrap
DDL，部署后的 TurboDB/ORM/DataBind 运行时不会因为数据库 DDL 生成而引入 TBE parser 依赖。

## TBE Compiler

- 编译器选项与可运行示例：[`tbe/tbe_compiler/CLI_OPTIONS.md`](tbe/tbe_compiler/CLI_OPTIONS.md)
- 数据库 DDL 设计与边界：[`docs/architecture/tbe-database-ddl-generation.md`](docs/architecture/tbe-database-ddl-generation.md)
- DataBind 运行时说明：[`tbe/data_bind/README.md`](tbe/data_bind/README.md)

数据库语言生成空库初始化所需的表、外键、普通/唯一复合索引、自定义 `CHECK` 与种子 `INSERT`。
它不是 migration engine，不会比较线上结构、生成 `ALTER TABLE` 或连接数据库执行迁移。

SQLite `uint64` 以受严格约束的 canonical decimal `TEXT` 保存，避免大整数经 REAL 静默舍入；
PostgreSQL 字符串默认值使用与 `standard_conforming_strings` 无关的 escape literal。identity 能力按
dialect 校验：SQLite 仅接受有符号整数 rowid，PostgreSQL 接受 `uint8`/`uint16`/`uint32` 并保留值域
约束，但拒绝无法由 sequence 精确承载的 `uint64`。完整契约见上述数据库 DDL 设计文档。
