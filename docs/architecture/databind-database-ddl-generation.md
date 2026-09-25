# TBE 数据库初始化 DDL 生成

## 背景与目标

TurboDB 当前允许调用方执行原始 DDL，但不会从模型生成 SQLite 或 PostgreSQL schema。TBE 已经是字段类型、可选性、默认值和绑定 annotation 的事实源，因此数据库初始化结构也应由同一份 DataBind IDL 在构建期生成。

本设计在 `databindc` 中增加 SQLite 与 PostgreSQL 两种输出语言。编译器先把带数据库 annotation 的 message 校验并归一化为数据库 schema IR，再由内置 Mustache 模板输出确定性的 bootstrap DDL。TurboDB、ORM 与 CFlow 运行时不解析 TBE，也不新增 SaltsUtils 运行时依赖。

当前版本生成全新数据库所需的 `CREATE TABLE`、外键、普通/唯一复合索引、自定义 `CHECK` 和种子 `INSERT`，并以标准 `BEGIN; ... COMMIT;` 包裹完整文件。它不比较线上结构，不规划已有数据库的 migration，不维护 migration history，也不执行数据库连接。

## 公开接口

命令行新增两种语言及 PostgreSQL 短别名：

```text
databindc model.schema --lang sqlite --output schema.sqlite.sql
databindc model.schema --lang postgresql --output schema.postgresql.sql
databindc model.schema --lang postgres --output schema.postgresql.sql
```

数据库输出要求显式给出 `--output`。`--source-output`、`--guest-output` 与 `--dsl-output` 仅属于 C 代码生成；与数据库语言组合时编译器立即报错。

数据库语言也可与 `--template` 组合。自定义模板消费的是已经校验、归一化后的数据库 IR，而不是未经约束的原始语法树；内置模板仍是默认路径。

## Annotation 契约

只有带 `db_table` 的 message 会生成表。未标注的 message 继续服务于网络消息或绑定，不会意外进入数据库结构。

```tbe
schema Accounts [
    id(1), version(1), byte_order(little),
    db_init(sqlite, "INSERT INTO users(id, tenant) VALUES (1, 7)")
];

[db_table("users"), db_unique_index(users_identity, id, tenant)]
message User {
    [db_column("id"), db_primary_key(1)] int64 id;
    [db_primary_key(2)] int32 tenant;
    optional string display_name;
}

[db_table("orders"),
 db_foreign_key(fk_orders_user, User, user_id, id, tenant, tenant),
 db_index(idx_orders_lookup, tenant, user_id),
 db_check(ck_total, sqlite, "total_cents >= 0")]
message Order {
    int64 user_id;
    int32 tenant;
    int64 total_cents;
}
```

支持的 annotation：

| 位置 | Annotation | 语义 |
|---|---|---|
| message | `db_table("name")` | 将 message 映射为表，值是未引用的逻辑标识符 |
| message | `db_foreign_key(name, Target, local, remote, ...)` | 创建同一 schema 内的单列或复合外键；字段参数必须成对出现 |
| message | `db_index(name, field, ...)` | 按声明顺序创建普通单列或复合索引 |
| message | `db_unique_index(name, field, ...)` | 按声明顺序创建唯一单列或复合索引 |
| message | `db_check(name, dialect, "expression")` | 为指定 `sqlite` 或 `postgresql` dialect 创建表级检查约束 |
| message | `db_foreign_key_on_delete(name, action)` | 为同一 message 中已声明的外键添加 `CASCADE`、`RESTRICT` 或 `NO ACTION` 删除动作 |
| schema | `db_init(dialect, "INSERT ...")` | 在所有表和索引之后按声明顺序输出该 dialect 的种子 INSERT |
| field | `db_column("name")` | 覆盖列名；缺省使用 TBE 字段名 |
| field | `db_primary_key(order)` | 将字段加入主键；序号从 1 开始且必须连续、唯一 |
| field | `db_unique(1)` | 为单列增加 `UNIQUE` 约束；只接受 `1` |
| field | `db_generated(identity)` | 数据库生成整数主键；v1 只接受 `identity` |
| field | `db_ignore(1)` | 不把字段持久化；只接受 `1`，且不能与其他 `db_*` field annotation 并用 |

annotation 参数以有序 `values` 列表进入 AST；兼容字段 `value` 继续保存第一个参数。既有单参数 annotation 仍要求恰好一个参数，多余参数会 fail fast。

外键目标必须是同一 schema 中带 `db_table` 的 message，且远端字段序列必须精确等于主键、单列 `db_unique(1)`，或一项 `db_unique_index`。本地与远端字段必须可持久化且归一化 SQL 类型一致。索引名称在整个 schema 内唯一，并且不能与表名冲突；约束名称在所属表内唯一。SQLite 调用方必须像其他 SQLite 外键方案一样在连接上启用 `PRAGMA foreign_keys=ON`。

`db_check` 表达式和 `db_init` 语句是构建输入中的受约束 SQL 片段。编译器拒绝控制字符、分号、SQL 行/块注释、未闭合引号和不平衡括号；`db_init` 还必须以独立的 `INSERT` 关键字开头。编译器不解析完整 SQL 语法，最终合法性仍由目标数据库验证。非当前 dialect 的片段不输出，但仍执行安全与结构校验。

标识符由编译器按 SQL 标准双引号规则转义。调用方传入的是原始名字，不能传入已经带引号的 SQL 片段。空字符串与包含 NUL 的名字非法。SQLite 和 PostgreSQL 都保留原始大小写，因为输出始终引用标识符。

## 字段与约束语义

- 普通字段与 `required` 字段生成 `NOT NULL`。
- `optional` 字段允许 `NULL`。
- 主键字段始终生成 `NOT NULL`；因此 `optional` 与 `db_primary_key` 组合非法。
- TBE `default` 只允许布尔、整数、浮点、字符串和 enum 常量。十进制数值可包含正负号、小数或指数，十六进制整数沿用既有语法；这些扩展数值 token 仅在字段 `default` 后接受，enum/flags 赋值、numeric annotation 与定长字段仍沿用原有非负整数规则。非法或不完整的数值 token 在 parser 阶段失败。编译器根据字段类型生成 typed SQL literal，不接受任意 SQL 表达式。
- `db_generated(identity)` 必须用于单列整数主键，不能与 `optional`、TBE `default` 或 `db_unique` 组合。
- 多列主键按 `db_primary_key(order)` 排序，表级输出；序号有重复或缺口时失败。
- 表名和列名在同一 schema/table 内必须唯一。比较采用字节级精确匹配；数据库自身更严格的名称规则由执行 DDL 的数据库报告。
- 标注为表的 message 至少要有一个未忽略字段。

## 类型映射

类型映射必须保持 TBE 可表达值域；无法无损表达的整数使用精确十进制类型，而不是静默缩窄。

| TBE 类型 | SQLite | PostgreSQL |
|---|---|---|
| `bool` | `INTEGER` + `CHECK (col IN (0, 1))` | `boolean` |
| `int8`/`int16`/`int32` 及别名 | `INTEGER` | `smallint`/`integer` |
| `int64` 及别名 | `INTEGER` | `bigint` |
| `uint8`/`byte` 及别名 | `INTEGER` + 值域 `CHECK` | `smallint` + 值域 `CHECK` |
| `uint16` 及别名 | `INTEGER` + 值域 `CHECK` | `integer` + 值域 `CHECK` |
| `uint32` 及别名 | `INTEGER` + 值域 `CHECK` | `bigint` + 值域 `CHECK` |
| `uint64` 及别名 | canonical decimal `TEXT` + 格式/值域 `CHECK` | `numeric(20,0)` + 值域 `CHECK` |
| `float`/`f32` | `REAL` | `real` |
| `double`/`f64` | `REAL` | `double precision` |
| `string` | `TEXT` | `text` |
| `bytes` 与定长 `bytes(n)` | `BLOB` | `bytea` |
| `uuid` | `TEXT` | `uuid` |
| enum 引用 | 按 underlying integer 映射及校验 | 按 underlying integer 映射及校验 |

SQLite 的 `uint64` 使用 canonical decimal `TEXT`，从而避免 NUMERIC affinity 把超过有符号 64 位范围的值转换为不精确的 REAL。其约束只接受 ASCII 十进制数字、要求 BLOB 字节长度与文本字符长度一致（因此拒绝 embedded NUL 与多字节非 ASCII 文本）、拒绝前导零（值 `0` 除外），并限制到 `18446744073709551615`；默认值也以文本 literal 输出。SQLite 的 `bool`、`uint8`、`uint16`、`uint32` 与 enum 整数约束同时要求 `typeof(col) = 'integer'`，因此不会接受 REAL 分数；`optional` 字段仍显式允许 `NULL`。

SQLite 的 `INTEGER PRIMARY KEY` 对应 rowid，只有这种单列有符号整数主键可以使用 `AUTOINCREMENT`，所以 SQLite 拒绝所有无符号 identity。PostgreSQL 的 identity 使用 `GENERATED BY DEFAULT AS IDENTITY`：`uint8`、`uint16`、`uint32` 分别由 `smallint`、`integer`、`bigint` identity 承载并保留无符号值域 `CHECK`；`uint64` identity 因 PostgreSQL sequence 不支持 `numeric(20,0)` 而明确拒绝。

PostgreSQL 字符串默认值始终使用 escape string literal（`E'...'`）：反斜杠编码为 `\\`，单引号编码为 `''`。该规则不依赖服务器的 `standard_conforming_strings` 设置，且 annotation 值永远不会作为原始 SQL 拼接。

collection、map、group、composite、union 引用不做隐式 JSON/BLOB 序列化。数据库表中遇到这些字段会失败，除非字段显式使用 `db_ignore(1)`。这样可避免同一数据同时受 TBE wire layout 与未声明数据库编码规则支配。

## 编译流水线与状态归属

```text
TBE text -> parser/annotator -> database validation + normalized IR -> Mustache -> DDL file
```

- TBE AST 是输入事实源。
- 数据库 IR 是单次编译内的派生只读视图，拥有自身节点并在编译结束释放。
- dialect 只负责类型名、identity 片段和值域约束等策略差异；annotation 规则、标识符处理、主键排序与默认值校验共用一套实现。
- SQLite 在建表语句内生成外键；PostgreSQL 先创建全部表和索引，再用 `ALTER TABLE ... ADD CONSTRAINT` 创建外键，因此目标 message 可后置声明，引用 `db_unique_index` 时该索引也已存在。initializer 始终最后输出。
- SQLite 与 PostgreSQL 内置模板都直接生成文件级 `BEGIN; ... COMMIT;`；执行方不得再添加外层事务。
- 模板只负责排列已经转义的片段，不承担业务校验，不拼接未经验证的 annotation 值。
- 输出文件沿用现有编译器写入边界：解析、校验或渲染失败时返回非零，不产生可被误认为成功的半成品结果。

数据库 IR 对模板暴露以下稳定形状：

- schema：`db_tables`、`db_indexes`、`db_initializers`；
- table：`sql_table_name`、`db_columns`、`db_primary_key_columns`、`db_foreign_keys`、`db_checks`、`has_composite_primary_key`、`has_next_table`；
- column/主键引用：既有 `sql_column_name`、类型、约束和对应 `has_next_*`；
- foreign key：`sql_constraint_name`、`sql_referenced_table_name`、`db_foreign_key_columns`；每个列对提供 `sql_column_name`、`sql_referenced_column_name`、`has_next_foreign_key_column`；
- index：`sql_index_name`、`sql_table_name`、`db_index_columns` 和仅在唯一索引出现的 `is_unique_index`；索引列提供 `sql_column_name`、`has_next_index_column`；
- check：`sql_constraint_name`、`sql_check_expression`；initializer：`sql_statement`。

`has_next_*`、`has_sql_constraints` 和 `is_unique_index` 只在值为真时出现。`is_last` 不属于数据库 IR 契约。所有 SQL 名称、类型和约束文本都已经归一化；模板不读取原始 `attributes`。

例如，自定义数据库模板可以只使用稳定字段生成多表分隔、列逗号和复合主键顺序：

```mustache
{{#db_tables}}CREATE TABLE {{sql_table_name}} (
{{#db_columns}}  {{sql_column_name}} {{sql_type}}{{#has_sql_constraints}} {{sql_constraints}}{{/has_sql_constraints}}{{#has_next_column}},{{/has_next_column}}
{{/db_columns}}{{#has_composite_primary_key}}, PRIMARY KEY ({{#db_primary_key_columns}}{{sql_column_name}}{{#has_next_primary_key}}, {{/has_next_primary_key}}{{/db_primary_key_columns}}){{/has_composite_primary_key}}
);{{#has_next_table}}
{{/has_next_table}}{{/db_tables}}
```

输出采用同目录、独占创建的临时文件，成功后替换目标。POSIX 首次创建以 `0666` 建立临时文件并由进程 `umask` 收窄；覆盖既有目标时，替换前把目标的 permission bits 复制到临时文件，因此 `0600`、`0640` 等既有 mode 不会被放宽。Windows 仍使用现有的 `_S_IREAD | _S_IWRITE` 创建模式和 `MoveFileEx` 替换边界；本契约不声明 Windows ACL 继承或保留保证。

## 错误语义

以下情况立即失败，并在错误中包含 message/field 与 annotation 上下文：

- 数据库语言没有任何 `db_table`；
- annotation 缺值、值非法、重复或组合冲突；
- 表/列重名，或表没有可持久化列；
- 主键顺序非法；
- 外键字段不存在、类型不一致或目标字段不具备唯一性；
- 索引名称/字段重复或索引字段不存在；
- CHECK/初始化 dialect 未知，或 SQL 片段越过单语句安全边界；
- 类型不能映射或 identity 不能由目标 dialect 表达；
- 默认值不符合字段类型；
- 数据库语言与仅适用于 C 生成的附加输出选项组合。

不提供回退到 `TEXT`、`BLOB` 或原始类型字符串的路径。

## 候选方案与取舍

### 仅提供示例 Mustache 模板

改动最小，但通用 Mustache provider 无法可靠筛选任意 annotation，也无法集中验证主键序号、类型值域和 identity 差异。错误会推迟到部署时，因此不采用。

### 在 TurboDB/ORM 运行时生成 DDL

可以靠近数据库连接，但会让 ORM 运行时依赖 SaltsUtils/TBE，并让模型解析进入数据路径。它也会把 schema 生成与迁移执行混为一体，因此不采用。

### 独立数据库 schema 编译器

边界清晰，但会复制 TBE CLI、资源查找、AST 与安装逻辑。当前只有两个 dialect，不足以抵消维护成本，因此数据库 DDL 继续作为 `databindc` 的语言策略实现。

## 兼容性、迁移与回滚

现有语言枚举值、现有模板和默认 C 输出不变；SQLite/PostgreSQL 使用新增枚举值。没有 `db_table` 的既有 schema 在既有语言下行为不变，只有显式选择数据库语言时才要求数据库 annotation。

采用方先在构建产物中生成 DDL，并在空数据库执行验证；现有手写 migration 仍是线上升级的事实源。确认初始化 DDL 与既有结构一致后，才可用于新部署。回滚只需停止调用新的语言输出并恢复原手写 bootstrap DDL，不涉及数据迁移或运行时二进制兼容。

## 验证范围

- 编译器单元测试覆盖两种 dialect 的逐字节确定性输出、alias、标识符转义、默认值与所有非法组合。
- SQLite 输出由编译器单元测试验证确定性文本、约束形状与 canonical `uint64` 边界；采用方负责按迁移流程在目标 SQLite 版本执行生成 DDL。
- PostgreSQL 输出先做 golden contract；合入前按远程测试 runbook 在真实 PostgreSQL 16 容器、`standard_conforming_strings=off` 会话中执行生成 DDL，逐字节检查注入形状字符串默认值并检查约束。
- 运行现有 `test_databindc` 与相关 CTest 回归，证明原语言输出不变。
- 安装后从安装目录运行 `databindc`，证明两份内置模板随工具安装。
