# DataBind 直接 Reader 转换与 TurboDB 单引擎迁移

日期：2026-09-20。
状态：保留 2026-09-20 的设计与验收基线；后续实施以关联 issue/PR 的精确提交和测试为准。DataBind 属于 SaltsUtils，唯一公开消费目标为 `Salts::Databind`。

任务归属：

- [DataBind 转换入口：salts-utils #99](https://github.com/qigao/salts-utils/issues/99)。
- [TurboDB 消费方迁移：turbodb #52](https://github.com/qigao/turbodb/issues/52)。
- [CBind 消费者盘点与退役：salts #305](https://github.com/qigao/salts/issues/305)。

本规范位于 SaltsUtils 的 `tbe/` 组件目录。DataBind 的源码、构建、安装、发布与问题跟踪均归属 SaltsUtils；不设独立项目、package/root 或转发包。

## 1. 决策与已核对的事实

本文件延续 [DataBind-only 设计](../../../docs/superpowers/specs/2026-09-12-databind-convergence-design.md)（正文标记 Approved on 2026-09-13）及已完成的 [#47](https://github.com/qigao/salts-utils/issues/47)。旧设计限定 SaltsUtils 自身，不曾授权立即删除未知上游消费者；本次用户批准扩展到 TurboDB，并通过独立盘点/破坏性变更阶段退役 CBind。

核对基线：

| 仓库与提交 | 事实，不是未来能力声明 |
| --- | --- |
| salts-utils `615851439b939dc0692a410192e3bc79e0cec413` | `tbe/data_bind/CMakeLists.txt` 的 `DataBindCore` 只编译 format-provider 源码。`tbe_typed.c` 仍在 DataBind 聚合目标中，聚合目标私有链接具体格式适配器。 |
| turbodb `a37c1183ec53e4d1bb9a7b938b50d86d9688a3b2` | `orm/src/flow/orm_cbind_publisher.c` 直接调用 `cbind_decode()`，同时持有 cursor 和 Publisher 生命周期状态。 |
| turbodb PR #44 的现有报告 | run `35506650767` 记录新 28 例中 14 通过/14 失败，旧 93 例通过；这是已记录的 post-construction RED，不是本次重新运行。 |

依赖目标名或 PUBLIC 链接列表不能代替实际运行时闭包证据。SaltsUtils 导出消费目标，也不能单独证明直接 reader 的轻量转换入口已经完成。

## 2. 唯一职责归属

| 层 | 主事实源 | 禁止接管 |
| --- | --- | --- |
| CMeta | native 类型身份、结构、字段、offset、alignment、生命周期适配 | schema wire 策略、SQL 生命周期 |
| CSerde | reader/token 协议、视图标记与源错误 | 业务对象构造、connection 调度 |
| DataBind | schema overlay、native/dynamic 转换、限制、诊断与失败回滚 | SQL、transaction、cursor cancel/destroy、WAIT |
| TurboDB | native 调用准入、connection/transaction/query/cursor 持有、清理与数据库错误 | 通用绑定引擎、第二套反射模型 |
| CFlow | Publisher/消费/调度协议 | 推断数据库事务结果或对象持有 |

DataBind 是唯一绑定引擎。不委托 CBind，不增加 engine selector、runtime fallback、兼容转发符号或第二套 native descriptor。

“统一”不是要求所有目标强行走一个巨型函数。native 与 dynamic 可以有各自存储 provider；但结构、转换规则、上限和回滚必须共用受审查的事实源，不能各自维护一套类型系统或可互相 fallback 的引擎。

## 3. 目标调用路径

```text
native database driver / cursor
    -> ORM native reservation + owner holds
    -> cserde_reader
    -> DataBind conversion core
    -> complete native row
    -> ORM row Publisher / CFlow
```

普通 CMeta native 行不需要 schema 文本、TBE 代码生成或动态 DOM。schema alias/default/presence/wire 信息仅在明确需要时通过 DataBind overlay 提供。不能用 schema/wire 元数据猜测任意 C ABI。

native 行路径不得强制经过 JSON 或先构造 `DataBindValue` 再转回 C struct。dynamic 输出是另一种显式目标，并不意味着第二个绑定引擎。

## 4. 直接 Reader 输入契约

### 4.1 输入与版本

接口必须能够表达：

- 已打开且由调用方持有的 `cserde_reader`；
- canonical `cmeta_data_desc`；
- 明确的资源预算/工作区与目标 native storage；
- DataBind 自身的版本化错误和诊断输出；
- 需要时的既有 DataBind overlay，而非重新声明字段 offset/kind。

正式头文件、函数名、context 布局、版本号由 #99 的首个接口审查任务冻结。本设计不把示意签名当成已经存在的 API，也不提交无实现的公共函数。

`DataBindStatus` / `DataBindError` 是错误主边界。需要保留 reader 原因或扩展诊断时，先审查版本化布局/调用约定，不能增加一套 CBind 错误别名，或悄悄改变既有 ABI。

### 4.2 同步消费与边界

一次成功调用恰好消费一个完整 canonical value；不得为了检查 EOF 偷读下一条记录。预检可判定的配置、描述或预算错误必须在读入前拒绝。

消费中失败不 rewind、不 replay。调用方通过显式错误决定如何处理源；DataBind 不负责 cancel、close 或释放 reader owner。reader 中的源错误不自动等于数据库连接断开。

本次不设计异步 decoder。ORM 可在 native cursor 层返回 WAIT；只有已取得完整行读取能力后才进入同步转换。DataBind 不持有网络 waitable，也不把部分解码状态偷渡到 CFlow。

### 4.3 原子输出与 semantic-zero

目标契约先采用明确的构造语义：目标由 descriptor 定义的初始化操作置为 semantic-zero/empty。不能把任意 C 对象的 `memset(0)` 当作正确初始化，也不能读取未初始化字节来猜测空状态。

使用受预算限制的临时目标构造，完成所有校验后才发布。失败销毁临时目标内全部已构造的 owned 字段，保留调用方原目标。非空目标不在本次构造入口内隐式 replace；不能支持的目标应在消费前明确拒绝。

发布动作也必须有可证明的原子性：需要的生命周期/move/commit 契约在预检中确认。若 provider 无法保证安全移交或失败恢复，应返回明确不支持/描述错误，不用字节拷贝绕过生命周期。

临时 root、scratch、容器增长和 owned payload 均须有明确预算归属；深度、元素和 retained bytes 不得通过换入口变为无界。

### 4.4 借用与拥有

Owned STRING/BYTES 在源 slice 失效前复制。DataBind 可以支持明确契约下的 borrowed destination，但 `CSERDE_VIEW_STABLE` 仅描述源的约定寿命，不是永久有效承诺。

TurboDB 普通行结果不能逃逸一个在下一次 `next/cancel/destroy` 后失效的数据库缓冲区。没有独立 result owner/lease 的 borrowed 输出，应安全复制或明确拒绝；“解码期间锁住 connection”不能替代结果自身的寿命。

### 4.5 错误类别

必须分别覆盖：无效输入/版本/descriptor，schema 映射缺失，类型与数值范围，源格式/截断，内存不足，配置资源上限，provider 生命周期失败。沿用 DataBind 已有分类；不足部分必须有明确 ABI/诊断审查。

在 TurboDB 边界只翻译一次错误，保留路径和源原因。native 已证明的 connection loss 才影响 connection 状态；普通类型/解码错误不能变成 connection failure。`COMMIT_UNKNOWN` 和 `CLOSE_FAILED` 不得被 binder 错误覆盖或“恢复”。

## 5. 最小依赖闭包

通用转换与回滚归并到 SaltsUtils 的 DataBind 内部实现，不再引入并列的通用 binder。格式 facade 依赖内部 core；core 不反向依赖 facade、具体 parser、schema compiler 或 CFlow。

消费者通过显式 `SALTS_UTILS_ROOT` 下的 `find_package(SaltsUtils CONFIG REQUIRED)` 获取唯一公开目标 `Salts::Databind`。SaltsUtils 封装内部依赖，消费者不拼接内部 targets、不引入独立 DataBind package/root 或兼容 alias。版本约束以实际公共 ABI 为准；仅替换链接目标名称不能证明轻量迁移完成。

验证以真实头文件、链接符号、目标闭包和适用平台的动态依赖为证据。复用现有构建/测试路径，不新增 CMake install/verify 框架、Python 行为测试、自动下载 fallback 或兼容 package。

## 6. TurboDB 切换与所有权的交汇门槛

DataBind 转换实现与 #28 的 cursor 正确性可以独立推进。消费者集成提交必须同时满足两条线：

```text
DataBind #99 direct-reader / lifecycle GREEN -----+
                                                +-> TurboDB #52 cutover
TurboDB #28 next/decode/cancel/dispose GREEN -----+
                                                     -> consumer full regressions
```

reservation 跨越 native `next` 和同步 reader 消费。拒绝准入时不得经旧通用错误路径立即调用 native cancel。忙连接上的 void cancel/destroy 必须保留原持有，安全时机清理且只执行一次；这仍属 ORM，不是 DataBind 的新队列或锁。

不为了取得一个绿色 binder 测试而删掉当前 28 个 RED 用例。保存旧 22 个 constructor 和其它 checked/transaction/control/cleanup/PG 断言；符号/文件重命名可机械更新，行为差异必须单独列出验证。

内部命名按职责收敛为 `orm_row_publisher`，不把整个生命周期层仅改名为另一种 binder。先检查是否有公开导出，不能未经审计假定所有名称都是私有。公开变化纳入明确 ABI 迁移。

## 7. 分阶段验收矩阵

| 组 | 必须有的证据 | 不能代替它的结果 |
| --- | --- | --- |
| 预检 | reader 调用次数为零、目标不变；版本/描述/预算边界 | 仅缺头文件的编译失败 |
| 值边界 | 恰好一个根值；第二条记录仍可读；截断与源错明确 | 整个输入被偷偷读空 |
| scalar/Struct | 真实 CMeta offset/type、signed/unsigned 范围、缺失/重复/未知字段策略 | 另造测试专用类型系统 |
| 生命周期 | owned text/bytes、多字段晚失败、释放计数、目标不变 | 只核对返回码 |
| 扩展类型 | enum/optional/NULL/container/variant 按已审计能力矩阵逐项支持或拒绝 | 根据类型名字承诺全支持 |
| 统一语义 | native/dynamic 重叠能力的对应输入、值、错误和回滚对照 | 强制 native 经 dynamic DOM |
| 依赖 | 直接 native consumer 的真实最小闭包 | 只检查 PUBLIC_LINK_LIBRARIES |
| ORM 集成 | SQLite、C/C++ row、错误与 owner、既有 live PG、完整无过滤 CTest | 旧 head 的绿灯或 fixture stub |
| 平台/工具 | 精确代码和依赖提交、适用 Windows/Linux/Debug/Release/sanitizer | 没有 tests、上游失败后 skip |

不固定声称所有矩阵项已经支持；实现前将当前 CBind 消费方所用能力与 DataBind 已有能力逐项对齐。未达到消费者实际覆盖前不执行 cutover。

## 8. CBind 退役门槛

salts #305 先完成明确范围的消费者盘点，涵盖默认分支、活跃分支、示例、测试、构建、SDK 与当前文档。不可访问仓库和外部下游必须记录为未知，不能用一次空搜索证明不存在。

算法或测试迁移保留来源/许可；不得把 CBind 复制进 DataBind 后作为独立内部引擎继续维护。只有已登记消费者完成直接 DataBind 迁移及完整验证，才在上游破坏性版本中删除目标、源码、公开头与导出。

不提供 `Salts::CBind -> DataBind` alias，不做运行时 fallback。不删除 CMeta、CSerde、CSTL、CFlow 等共享基础。旧证据/历史设计保留为档案，当前架构文档明确新 owner。

## 9. 兼容性与回退

HIGH：DataBind 错误/上下文布局、结果初始化/移交、借用寿命、ORM ABI。所有公开变化先定版本与升级方式；旧 ABI4 和候选 ABI5 不混用对象布局。

MED：解耦格式库可能改变二进制依赖；临时构造可能改变内存峰值和吞吐。先记录当前基线再比较，不承诺性能提升，也不为性能跳过安全复制。

LOW：内部文件/符号命名。与语义变更分开审查，保留测试一一对应关系。

回退只能回退整个有版本的源码/依赖 pin 集成提交，不是在运行中切到另一 binder。当前 docs/issue 阶段不更改 production、不关闭 #28/#30、不 merge #44。

## 10. 首批交付与明确非目标

执行顺序见 [实施计划](../plans/2026-09-20-native-reader-and-turbodb-cutover.md)。第一批是接口/能力盘点和真实 reader 契约测试，然后在 DataBind 内归并实现；不是在 TurboDB 里再写通用 decoder。

本任务不自动完成 MySQL driver、runtime factory/loader、module unload、异步 WAIT、最终 ABI5/C++ 迁移或 SDK Task6。它们仍使用各自的验收门槛。
