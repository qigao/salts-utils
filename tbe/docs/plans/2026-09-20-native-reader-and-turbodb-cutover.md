# DataBind Native Reader / TurboDB Cutover 实施计划

日期：2026-09-20。
关联：[规范](../specs/2026-09-20-native-reader-and-turbodb-cutover.md)、[DataBind #99](https://github.com/qigao/salts-utils/issues/99)、[TurboDB #52](https://github.com/qigao/turbodb/issues/52)、[Salts #305](https://github.com/qigao/salts/issues/305)。

本文件保留 2026-09-20 的计划状态，不是当前验收报告；后续结果以关联 issue/PR 的精确提交和测试为准。

DataBind 是 SaltsUtils 的组成部分，唯一公开消费目标是 `Salts::Databind`，不设独立 DataBind package/root。

## Task 1 — 建立精确能力与消费者基线

所属 #99 / #52 / #305；首先执行。

- [ ] 在当前真实 checkout 固定 Salts、DataBind 源码 owner 和 TurboDB PR #44 的提交，不用重建的源码子集冒充完整构建。
- [ ] 阅读 `tbe/data_bind/tbe_typed.c`、`data_bind*.c/.h`、现有 CMeta/typed 测试，标出可共享的验证、scalar/enum、生命周期、回滚函数及其调用者。
- [ ] 盘点 TurboDB `orm_cbind_publisher.c/.h`、公开流接口、`orm/CMakeLists.txt`、ownership/flow/PG测试与示例的真实能力，不根据 CBind README 推断所有类型均被使用。
- [ ] 记录类型矩阵：输入 token、native CMeta 形态、可选 overlay、现有 DataBind 路径、目标 direct-reader 路径、错误/上限/生命周期用例。未支持列显式标记，不从测试空白推导支持。
- [ ] 记录已知 CBind 消费者及未知范围；Salts默认分支搜索和TurboDB活跃分支搜索分开。
- [ ] 保存现有失败门槛，特别是 #44 post-construction 28例的实际结果；不得先移除这些测试。

产物：基线清单、能力差异和真实测试证据。现有 #47 已完成的 CMeta迁移只复用，不重开为平行类型系统。

## Task 2 — 冻结直接 Reader 的 C 契约与真实 RED

所属 #99；依赖 Task 1。

- [ ] 从现有 DataBind 错误/版本化上下文中定出可扩展的直接 reader 接口，明确工作区、临时root和owned payload预算归属。
- [ ] 明确 descriptor-defined semantic-zero 构造、非空目标拒绝、原子发布的生命周期要求；不能要求所有类型都支持任意 bytewise move。
- [ ] 明确读取恰好一个value、不偷读下一value、预检不读输入、失败不rewind、reader/owner不被释放。
- [ ] 明确 owned/borrowed 结果策略、source error 的归属和诊断存活期；不得把所有 reader error 都映射成数据库 connection error。
- [ ] 在 DataBind 现有 C/TinyTest 框架新增可观察 reader：计数每个token/read、标记slice失效、故障注入、记录生命周期次数；不得在fixture里实现decoder或额外维持production持有。
- [ ] 先记录接口缺失/链接失败，再取得行为RED。两类证据分开，不能把编译不过称为错误行为已复现；不发布空实现来凑公共ABI。

首批用例必须覆盖：scalar/Struct真实字段、同一reader连续两个值、预检零读取、尾字段失败的回滚、深度/元素/字节上限、source-error、owned transient bytes复制和context重用。

## Task 3 — 归并 DataBind 转换核心并取得 GREEN

所属 #99；依赖 Task 2。

- [ ] 把现有 typed/native 转换中的格式无关部分下沉到 DataBind-owned core。直接 reader 和既有路径调用同一语义/生命周期实现，而非平行复制。
- [ ] schema/具体parser适配留在上层；纯 CMeta native 调用不需要 schema文件或代码生成。native路径不构造JSON或强制DataBindValue中间树。
- [ ] 原子构造与完整rollback先成立，再扩展 Task 1 确认必需的 enum/optional/container/variant 能力；明确不支持的组合消费前fail-fast。
- [ ] 用 native/dynamic 重叠输入验证值语义、边界错误和失败回滚的一致性，不要求两个目标内部存储相同。
- [ ] 删除本次归并后无调用者的重复 helper，保留来源与测试。不得新增独立 CBind engine、target、wrapper、feature flag 或fallback。
- [ ] 恢复所有新行为用例GREEN并运行未改写的 typed/generated/dynamic 回归与适用sanitizer；负对照只作补充，不替代真实旧实现RED。

退出条件：所需能力全部有直接 reader 的真实运行证据；不存在仅以替代reader/替代metadata来掩盖的集成缺口。

## Task 4 — 验证最小消费闭包与发布契约

所属 #99；依赖 Task 3。

- [ ] 在 SaltsUtils 内复用 DataBind 转换实现，通过唯一公开目标 `Salts::Databind` 导出真实已实现的接口与版本要求；不让消费者组装内部 targets。
- [ ] 用现有构建流程编译C/C++直接-reader消费者，核对真实链接/动态依赖，不只是 PUBLIC targets。
- [ ] 验证内部 direct native 转换不依赖 CBind、具体 parser、schema compiler 或 CFlow；可选适配不成为内部 core 的反向依赖。该检查属于 SaltsUtils 的实现闭包验证，不改变 `Salts::Databind` 的唯一公开消费入口。
- [ ] Windows/Linux、static/shared的适用组合与既有sanitizer验证通过；不把某配置下未构建的backend列成已验证。
- [ ] 通过正式 SaltsUtils 安装消费 DataBind；不得生成第二份运行时、独立 package/root、forwarding 包或兼容别名。

禁止为此新增 CMake install/verify框架或Python行为测试。需要的失败检查放在实际配置入口，功能证据来自编译与真实运行。

## Task 5 — 与 ORM 所有权修复汇合后切换消费者

所属 TurboDB #52。

硬前置：Task 4 GREEN，以及 #28 的相关 next/decode/cancel/dispose/cleanup 门槛 GREEN。两条线可并行，不得互相冒充完成。

- [ ] 固定 DataBind及Salts版本、审计公共导出/ABI影响，在独立迁移分支更新依赖。
- [ ] 按职责将内部 publisher 名称改为 `orm_row_publisher`；保留构造、终止、错误、取消、销毁的owner归属。
- [ ] 替换 CBind context/decode/error mapping，删除活跃 CBind includes/links/旧调用；一次切换，不增加双引擎开关。
- [ ] 将 DataBind完整同步读取包含在ORM reservation内，保存 source/owned row 的寿命边界。
- [ ] 原有测试用例保留行为断言；机械名称更新建立一一对应。新增转换差异/错误映射用例不能取代28例重入RED。
- [ ] 运行SQLite、C/C++、现有PG正常/断连/取消、ownership、独立SDK及无过滤root CTest。记录精确提交、依赖pin、测试清单、failed/skipped状态；全绿后才满足消费者切换门槛。

Binder变更本身不关闭 #28 的ABI5/最终cleanup门槛，也不完成 #30 SDK Task6。

## Task 6 — 其他消费者与 CBind 上游退役

所属 Salts #305；依赖 Task 5 及所有已登记消费者迁移。

- [ ] 复查活跃分支和新消费者；未知/无法访问范围明确列出。
- [ ] 每个已登记消费者绑定到DataBind-only可用版本，保留其独有类型/性能/资源要求证据。
- [ ] 在明确breaking release中删除 CBind实现/目标/公开头/导出/当前推荐，不增加alias或自动fallback。
- [ ] 保留相关历史记录与迁移来源；不删除CMeta/CSerde等共享基础。
- [ ] Salts与已登记消费者在对应精确版本上完成完整回归，才能关闭退役任务。

## 每个切片的报告格式

说明：基线head、修改head、实际文件列表、RED类型/首个失败、GREEN命令/退出码、测试数与跳过项、真实/模拟数据边界、sanitizer实际启用情况、未验收范围。

修复前失败和中间失败不可抹去；没有测试、依赖构建失败后跳过、仅strict对象编译通过，都不等价于真实集成GREEN。禁止重试到绿、削弱断言或用改binder掩盖数据库执行错误。

## 本次文档阶段检查

本机未提供 `codegraph` 可执行程序；本阶段使用GitHub连接器直接阅读已知源码/设计/issue，没有声称完成调用图或完整消费者盘点。

当时的文档提交未修改生产代码、workflow、测试注册、依赖 pin、公共 ABI 或 PR #44 分支，也未取得新的 runtime 或 CI 绿色结果。后续实现状态由其独立的精确提交和验证记录决定；本次归属文字修正不改写历史结果。
