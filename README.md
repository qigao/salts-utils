# TurboParser

TurboParser 提供统一 parser facade、Mustache、Cron、TBE schema、DataBind 与构建期代码生成工具。
运行时库与构建期工具保持分层：`tbe_compiler` 用于生成代码、RulesForge DSL 和数据库 bootstrap
DDL，部署后的 TurboDB/ORM/DataBind 运行时不会因为数据库 DDL 生成而引入 TBE parser 依赖。

## TBE Compiler

- 编译器选项与可运行示例：[`tbe/tbe_compiler/CLI_OPTIONS.md`](tbe/tbe_compiler/CLI_OPTIONS.md)
- 数据库 DDL 设计与边界：[`docs/architecture/tbe-database-ddl-generation.md`](docs/architecture/tbe-database-ddl-generation.md)
- DataBind 运行时说明：[`tbe/data_bind/README.md`](tbe/data_bind/README.md)

数据库语言当前只生成空库初始化所需的 bootstrap DDL。它不是 migration engine，不会比较线上结构、
不会生成 `ALTER TABLE`，也不会连接数据库执行迁移。
