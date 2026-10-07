# DataBind 3.0

DataBind 是 SaltsUtils 的组成部分，源码、构建、测试、安装和发布均由 SaltsUtils 负责。
消费者通过 `find_package(SaltsUtils)` 使用唯一公开目标 `Salts::DataBind`，
不使用独立 DataBind package/root，也不组装内部目标或补造兼容 alias。
生成代码、现有原生 C struct 与动态对象均通过 DataBind 绑定；不存在
DataBind 私有的 owning dynamic-container compatibility engine、storage fallback、第二 binder
或格式 fallback。原生对象使用 canonical CMeta graph 与 DataBind plans；历史
typed runtime、公开头文件与宏已物理删除，没有转发兼容入口。
已有 CMeta graph 的对象直接使用 `data_bind_native_init/clear`，Binary overlay
只参与 wire 验证和编解码。使用旧接口的调用方必须迁移到规范 native binding。
JSON/YAML/XML/CSV 原生路径使用 FormatPlan 和 MessagePlan/native，动态值转换使用
canonical value reader。BinaryLayoutIR 经验证后生成 BinaryLayoutPlan provider；
Binary reader/writer 通过 CSerde 与 MessagePlan/native 交互，不持有宿主字段偏移
或生命周期。当前准入 FIXED scalar/bytes/array、递归固定 record、GROUP、VAR_DATA string/bytes
与 COUNTED collection；集合后的固定字段使用 CURSOR_FIXED。
子 record 的范围必须与其固定块完全一致，拥有独立状态位并输出嵌套 MAP token；
执行只接受 ABI 2 和完整的当前 layout/field/element 记录；旧 ABI、旧尺寸记录在创建
lease 前被拒绝，调用方必须重新生成 provider。不推断缺失 representation 或 enum flags。
含可变 tail 的 inline 子 record 在修改对象或发布输出前返回明确错误。

Binary lease 由单线程持有，借用不可变 plan graph 与输入 wire 到 close，
容器栈硬上限为 `DATA_BIND_BINARY_LAYOUT_MAX_DEPTH`（32，含根 MAP）；
`max_depth` 可进一步缩小，0 选用该上限。reader 在发布前验证整个布局图与
active child 状态；writer 在消费 token 时限制 active MAP/ARRAY 深度。writer 子块复用
整条消息的 buffer，不为 child 分配 lease 或 buffer；token/容量错误阻止 sink 调用，
sink 失败明确返回错误，生成的有界 sink 保留完整旧输出。
布局校验复杂度为各次 record 访问的字段数平方之和（含重叠检查）；
读写遍历使用固定有界栈，保留既有按序字段查找与 positional VAR_DATA 规则。

GROUP 使用 canonical typed CSTL Vec 保存具备完整生命周期的 record，元素复制与释放
由 CMeta provider 负责，optional/nullable 状态归 MessagePlan 所有；该局部状态不授予
整个 record 的容器 move/copy 权限。Binary GROUP entry 必须完全固定，header 为
两个 `uint16`（stride、count），条数与单条固定块均不超过 `UINT16_MAX`。
读取允许 stride 大于已知固定块并跳过扩展字节；ABSENT 消耗完整 positional entry，
NULL 要求 count 为零，所有状态仍要求有效 stride。reader 在发布前检查乘法、截断及
active entry 状态，随后借用 wire 输出 ARRAY/MAP token；writer 在同一 buffer 中追加
entry，仅在 ARRAY_END 提交 count，在整条消息完成后发布一次。GROUP 占一个 ARRAY
和一个 entry MAP 深度，额外预检成本为所有 active entry 的固定字段访问总数。
可变 entry 的 Vec 所有权可用于文本绑定，Binary 在修改对象或发布输出前拒绝。

通用组合生命周期测试使用 CMeta Struct 与 CSTL 的受管 Vec、Set、Map provider，
覆盖嵌套 owner 的独立复制、释放、重复 clear、状态位复位和解码中途超限后的清理。
native storage 测试也直接使用 canonical native API，验证平台原生标量身份、
受管 string/bytes，以及 provider 定义的非全零 semantic zero 和恰好一次释放。
当前内部反射实现使用 Salts v2.1.0。容器声明与生成代码直接使用 `cmeta_type(...)`；旧 `typed(...)` 入口不再受支持。固定字节使用完整 CMeta exact fixed/buffer-v2 provider，精确长度赋值、借用读取、
独立 copy、清零源对象的无分配 move 与幂等 restore 共享同一 inline 存储。
Binary FIXED BYTES 的 `scalar_bits` 为零，`wire_extent` 是唯一 wire 长度事实源；
不添加长度前缀、不做端序转换。reader 借用完整 wire span 到 close，writer 在
精确长度校验后复制到消息 buffer，仅在整条消息完成时发布。native/MessagePlan
仍要求完整 owned buffer provider；只有 fixed copy/restore 的自定义形状继续被拒绝。
固定数组使用 SDK 的 canonical fixed-sequence provider，不借用 raw composite init/clear
模拟 canonical 所有权。Binary/native 回归覆盖 wire 黄金字节、大小端、
ABSENT/NULL/VALUE、独立 owner、重复 clear、失败回滚和有界输出的一次性发布。

Binary runtime 已支持固定数组 wire plan：字段仍使用 FIXED/ARRAY_BEGIN，
`array_plans` 按数组字段显式提供，记录 count、元素 token、位宽、enum flags 与
wire extent；record 元素复用同字段的 child plan。该表不承载 CMeta/native 状态，
与宿主字段偏移无关。执行要求完整的当前计划，不接受旧尺寸 child-layout 或 element 记录。
count 与元素 extent 的乘法必须经 checked arithmetic 后等于字段 extent。
reader 在发布前检查 active record 元素的状态，借用整段 wire；writer 直接写入
预先验证的固定块，精确收到 count 个元素后才接受 ARRAY_END，短输入、超长输入、
错误 token 与越界数值均阻止整条消息发布。不增加 count header，不做 native-offset
推导，也不为元素分配独立 lease/buffer。固定数组占一个 ARRAY 深度，record 元素再占
一个 MAP 深度；预检成本为 active record 元素的字段访问总数。
生成/native 固定数组使用 canonical provider 与共享 compiler lowering；未满足这些
契约的入口返回明确错误，不生成替代存储或兼容执行路径（[#489](https://github.com/qigao/salts-utils/issues/489)）。

DataBind 是 SaltsUtils 中的 schema 驱动纯 C 运行时。它解析 schema、构造动态值、校验字段，
并统一处理 Binary、JSON、YAML、XML 和 CSV。它不加载或生成运行时代码，
运行时也不要求 C/C++ 编译器。

`salts-idlc` 与 DataBind 是两个不同层次：

- `salts-idlc`：构建期工具，把 schema 渲染为 `.h/.c`。
- `DataBind`：运行时库，为动态对象、现有 C struct 映射和生成代码提供公共
  bind/serialization 引擎。

## 设计边界

### Runtime 链接边界

运行时统一链接 `Salts::DataBind`，交付一个 `data_bind` 共享库。原 Core、JSON/YAML/CSV/XML、
Temporal、CMeta 与 CFlow 适配实现作为独立源码编入该库，使用统一的 `DATA_BIND_API` 导出。
这项调整解决了薄适配层分别安装、导出和部署带来的复杂度；源码分层仍负责维护格式、
反射与执行的职责边界。`Salts::DataBindPlugin` 仅为可选的 INTERFACE 集成目标，不产生二进制库。

保留拆分库可以让消费者只链接部分能力，但会继续维护多套产物和依赖入口；仅添加聚合
INTERFACE 目标也不能消除 DLL 部署链。因此选择合并 runtime，接受主库包含全部格式与
CFlow 适配代码的体积代价，不宣称性能收益。Parser/Query 实现依赖保持 PRIVATE，公开
头文件需要的 Salts 基础依赖由目标传递。Compiler、Producer、Schema 与 IDL 的既有目标
保留，不把构建期工具并入 runtime。

迁移时将旧 `DataBindCore`、`DataBind*Adapter`、`DataBindCMeta`、`DataBindCFlow` 链接项
替换为 `Salts::DataBind`，重新构建调用方，并使用干净的 SDK 安装目录；旧静态库和适配
DLL 不再安装，也不提供兼容目标。已编译的旧程序不能直接换用新包。函数签名、C 数据
布局、数据格式、对象所有权、状态提交及失败清理语义保持不变；C ABI 版本仍为 10。
回滚应恢复旧版 SDK 与对应调用方产物，不能混用两版库。验证范围包括 provider、native
生命周期、格式输出、CMeta/CFlow、C/C++ 消费与既有 package-config 测试。

成功准入的 Service 调用使用 `data_bind_binding_plan_bind_call` 与
`DataBindBindingCallLifetime` 建立一项整帧释放义务。入口先验证帧边界，初始化
request、IN/OUT staging、按值返回的 response 与 NONE error envelope，再绑定输入；
失败由 producer 回滚已初始化值，不发布 live 句柄。已发布的句柄不可复制、重置
或并发使用；它借用不可变 BindingPlan、descriptor domain、frame metadata 与 storage
到唯一终止点。不同 staging 必须互不重叠，request 在 ABI 参数表中的对应别名除外。

执行、egress、取消或延迟 finalize 后调用 `data_bind_binding_call_restore_zero`。
它使用 plan 缓存的 active-error DataDesc，按逆序恢复参数、request 与返回值，并清理
DataBind presence/null overlay；清理不需要 workspace、分配或 resolver 查询。终止前
验证帧与 discriminator，非法状态保留全部值与 live 义务；实际清理开始前消费句柄，
重复终止返回 `INVALID_ARG`，不以幂等析构掩盖重复调用。canonical 生命周期违反契约
时报告错误并继续恢复剩余 owner，已消费的句柄不能重试。

调用方只查询 producer 的 `data_bind_binding_call_is_live`，不另存 `frame_live` 或
逐字段释放位图。HTTP buffer、deferred response、executor task 与插件 domain lease
仍由各自 owner 终止，descriptor domain 必须最后释放。延迟任务通过既有同步交接
独占访问帧，不在执行期间修改 frame metadata。已有 `bind_inputs` 的失败回滚契约
保持不变，但不建立整帧句柄；迁移消费者须链接含新导出入口的 SDK 并重新编译。
验证范围包括失败 init、partial ingress、成功/typed-error、egress 失败、按值返回、
重复终止、无 workspace 的 warm 清理与安装后的 C/C++ 真实调用。
完整可编译调用见安装测试的
[Service call fixture](../compiler/package_config/databind_target/service_call_fixture.h)，
同一实现由公开 SDK 下的 C 与 C++ 消费者编译执行。

生成的 composite、group、message 描述符共用一份 record 模板。lowering 完成后，compiler
从原记录建立只供渲染使用的 `cmeta_records` 快照，由任务根节点统一拥有；构建失败释放
未发布快照并返回错误。字段行通过 Salts 2.1 的 `Schema/Replay` 同时生成 layout、semantic
field 和字段数量，每组最多 16 行，多个分组覆盖大结构。空字段列表不调用非空 Schema
宏，描述符计数仍为零。没有旧生成方式或 SDK 版本探测分支。

这里选择 Schema/Replay，而非直接使用 `cmeta_reflect_value`：生成图需要导出的
TypeDesc/DataDesc、泛型 declared-type 元数据和任意字段数量。字段地址统一由已有
`cmeta_once` 初始化，调用 `Type_cmeta_data()` 或 `Type_init()` 后才可读取导出图。
初始化按生成索引赋值，时间为 O(F)、描述符空间为 O(F)，F 为字段数；不再按名称搜索
外部 provider。layout 与 semantic 字段名统一使用 schema 名，满足 CMeta 的同名对应
契约；`[c(...)]` 只决定实际 C 成员及 `offsetof`，字段 stable ID 也使用 schema 名。
这修正了重命名字段无法通过 MessagePlan 准入的问题，并改变此类字段的元数据名称与 ID。
所有权、optional/nullable 准入与错误策略仍由各自 lowering 决定。
构建期快照增加与记录树大小成正比的临时存储，不新增运行时分配或依赖。

升级时重新生成并编译全部 schema 产物；不维护旧版产物的兼容路径。验证涵盖三组
33 字段、C 成员重命名、composite/group/message 生命周期、嵌套泛型及共享库导出图。
如需回滚，应回滚 compiler/templates 并整组重新生成，不能混用不同生成版本的头与源。

生成的 text decode helper 使用固定大小、栈内存储的 `cmeta_cleanup` 数组，
按取得顺序登记暂存 allocation、workspace、FormatPlan 和 native 临时值，退出时
由 `cmeta_cleanup_reverse` 逆序释放；编译器不再生成独立的 live 标志与清理状态机。
释放回调调用既有 owner 的无失败 API；已准入 CMeta provider 若违反无失败恢复契约，
立即终止，不把部分释放当作成功。义务记录、binding 与资源在同步调用内保持地址稳定。
codec 拥有的 MessagePlan 和 canonical reader view 均为借用，不进入释放列表。

FormatPlan 的输入别名表与输出名称表使用受管 CSTL Vec，元素的两个 `tstr` 由同一
CMeta value reflection 提供复制、移动和释放语义。`cmeta_reflect_value` 字段显式保留
`tstr` 的 owning provider；CSTL storage descriptor 只桥接相同 stable identity 与
反射生成的 traits，不维护第二份字段或生命周期实现；不从 `char *` 布局猜测
所有权。表容量分别受 schema 的输入名称总数与
根字段数约束，计数只由 Vec 维护。编译失败销毁整个未发布 plan，包括尚未填完的
元素；成功后 plan 独占名称存储，即使 codec 已释放也可查询。查询沿用线性扫描，
接受有长度的借用 slice，不分配内存；输出名称借用 plan，至 plan 释放时失效。

CSV 展平过程也使用受管 Vec：每个 cell 通过 `cmeta_reflect_value` 声明两个 owning
`tstr`（列路径与文本）。先插入空 cell，再原位填充；任何阶段失败均由 Vec 释放已完成
及部分完成的元素，不再额外遍历释放字符串。cell 指针只在下一次 Vec 修改前使用，
整个过程由同步调用独占；成功后输出仍是独立的序列化缓冲区，CSV 路径、CRLF 与错误码
保持原有约定。公开 API 回归覆盖多列增长、文本拒绝后的部分构建清理和再次序列化。

计划构建分为准入检查、未发布对象填充与成功发布。Schema 是名称和别名的事实源，
scan 仅在同步编译期间借用 schema；FormatPlan 保存派生快照，TransportPlan 独占其
ingress/egress 子计划。填充函数只返回错误，创建边界统一销毁部分对象；出口计划失败
会连同已成功的入口计划一起释放，调用方输出保持 NULL。发布后只读查询不推进状态，
info、canonical reader/writer 均借用 plan，必须先结束使用再释放 plan；构建和释放
由单线程 owner 执行，不允许与查询并发。

此处选择复用已有析构函数和 CMeta 元素生命周期，避免为两个堆对象再增加一套 scope
owner 状态。相比分散清理分支，失败归属集中；相比分拆为新子系统，不增加依赖层。
保留原有校验/分配顺序、错误码、容量限制、线性查找和公开 ABI，迁移仅限内部实现。
回退可恢复旧构建函数和元素描述，无需迁移数据或调用方。回归覆盖别名表增长、codec
释放后的名称查询，以及入口成功/出口失败后重试和 transport 快照的独立生命周期。

MessagePlan 保证失败解码的回滚，helper 只在解码成功后接管临时值；成功 move 后
立即解除源值义务，后续状态位复制失败也不会再次 restore 已移动的源值。
reader 按既有格式边界先关闭并检查结果，再发布临时值，这是逆序退出之外的显式
领域完成点；close 已消费 reader 义务，即使返回失败也不再重复关闭。
取得或解析失败保留原 destination，发布 move 失败保持既有 destination semantic-zero
行为和错误码。JSON/YAML/XML/CSV 的故障及正常路径由生成代码正式用例验证。

单层 `list<T>`、`set<T>`、`map<string,T>` 的真实原生存储分别是 CSTL Vec、Set、Map，
字段通过 `cmeta_declared_type` 发布对应 SDK constructor 和真实参数 TypeDesc。
生成的 typed facade 的 `cmeta_receiver_operation_set.owner` 使用同一 canonical constructor；
资格验证通过 CMeta 语义比较和 resolver 接纳操作，不用具体 specialization 名称、display
name 或描述符地址作为泛型 owner。构建期和消费端资格验证可检查这些元数据，生成的
执行路径仍调用普通 C API，不新增逐元素的 method/owner 查询。

optional/nullable 仍表示状态位加底层原生值，不投影成 `Option<T>`。当前 IDL/native
没有 Pair、Tuple 或 Result 的实际存储准入；直接请求这些泛型形式时拒绝生成，不因为
CMeta 已有 constructor 就合成原生表示。C++ 公共声明保持 C 布局，借用生成 C 产物发布的
canonical 图；跨 C/C++ 翻译单元通过 constructor stable identity 和递归参数身份比较。

嵌套泛型的原生 lowering 以不可变 Contract 中的递归逻辑类型为输入，在构建期按
内层到外层生成具体 CSTL wrapper。每层直接引用内层的 canonical TypeDesc/DataDesc，
COPY/MOVE/DESTROY 委托给 Salts 的 data-traits bridge。生成代码不增加容器生命周期
算法、合成 record 或运行时泛型注册表。字段 declared type 与 wrapper 的 APPLY identity
引用同一 SDK constructor 和递归参数身份；外部格式只由既有适配器解析。

构建期类型查询借用 Contract 文本，深度、节点数、文本长度分别限制为
`IDL_TYPE_REF_MAX_DEPTH`、`IDL_TYPE_REF_MAX_NODES` 和 `IDL_TYPE_REF_MAX_BYTES`。
生成器只准入已提供 canonical 生命周期的组合；容器缺少比较/哈希能力时不能作为
外层 Set 元素或 Map key。首批支持 List 和 string-key Map 的递归组合，叶类型限已发布
完整生命周期的 scalar、string、uuid 与 record；尚未公开完整 provider 的 enum 仍拒绝生成。
记录中的 optional/nullable 位仍归 MessagePlan，不改变
元素类型身份，也不授予含状态位 record 的整值复制权限。元数据参数引用在既有 once
初始化中按内层到外层构建，发布后不可变，执行期间不做字符串解析或名称查找。

首批嵌套 MessagePlan 支持 JSON 与 YAML 往返，包括空内层容器。XML、CSV 和 Binary
的现有表示能力不扩展；无法表示该类型时明确返回 schema/FormatPlan 错误。
带 optional/nullable 容器字段的记录可初始化和清理，尚未准入整值转换。

相比私有容器或扁平化存储，该方案增加构建期递归 lowering 和启动时 plan 资格验证，
保留原生存储、错误传播和单一生命周期归属。复制失败只清理未提交的内层 owner，
解码失败释放 staging，目标值和输出在完整成功后才发布。新增组合必须覆盖递归身份、
C/C++ 布局、独立复制、源对象清空、恰好一次释放、部分复制回滚和文本 round-trip。
已有单层字段的行为不变；依赖新组合的消费者必须重新生成并重编译。回滚先撤回这些
消费者，清空所有活跃 owner，再回滚 SDK 与生成器，不能混用不同能力的生成产物。

DataBind owns schema overlay, dynamic values and typed conversion semantics. CMeta remains the
canonical native semantic type model. Within SaltsUtils, the format-neutral conversion core
uses Salts foundation primitives; concrete format/query utilities remain adapters above that
internal boundary. This decomposition does not introduce another package owner.

规范所有权边界为：

```text
CMeta: native structure and semantic type graph
schema overlay: external names, presence/defaults, wire layout and validation
DataBind: native/dynamic conversion, rollback and format orchestration
CSTL: concrete container storage
CSerde/parsers: format tokens and mechanics
```

Generated/native and dynamic paths remain DataBind-owned over canonical CMeta structural
metadata; external names, presence/defaults, wire layout, validation, and fingerprints remain
overlay-only. DataBind 不复制结构类型事实，CSTL 不决定 schema，CSerde 与各 parser 也不执行
对象绑定。每个已发布的 runtime-schema dynamic root 保留一份 immutable recursive CMeta
identity graph；所有可达 child 借用其中与其 schema type expression 对应的 identity。
`DataBindValueKind` 仍是公开兼容与物理 storage union 的 discriminator，不承担另一套
semantic type graph。

动态值转换到 canonical native 对象使用 `data_bind_value_reader.h` 的
`data_bind_value_reader_open/close` 和既有 `data_bind_message_plan_decode_native`。
reader 借用不可变 value tree，只拥有打开时一次分配的有界遍历栈；调用方必须让
整棵树存活到 close，所有操作由单线程执行。节点、容器深度和累计借用字节数分别
受 `DataBindValueReaderLimits` 限制，超限通过 CSerde 返回明确失败。
对象字段已是 canonical 名称，不再做 FormatPlan 名称映射；string/bytes 保留不同的
token，UUID 输出 16 字节。datetime/date/time/duration/decimal/bigint/money 尚无本适配器
的 canonical 表示，直接返回 `CSERDE_UNSUPPORTED`。map 从实际存储 key 读取，不以文本
展示视图重建类型；现有动态构造入口仍仅接纳 string map key。
调用方先解码到 fresh staging，close reader 后才通过 CMeta move 发布；失败由
MessagePlan 清空 staging，原发布对象保持不变。native provider 拥有复制的 string/bytes，
动态树在 close 后可以释放。实现不引入序列化后重解析或 TBE compatibility wrapper。
完整调用和发布示例见 [native_value_reader_test.c](../tests/native_storage/native_value_reader_test.c)，
C++ 公开链接示例见 [native_value_reader_cpp_test.cpp](../tests/native_storage/native_value_reader_cpp_test.cpp)。

### 原生 parser 依赖与迁移

DataBind 3.0 makes the public ABI independent of parser/query implementation headers.
`data_bind.h` owns `DataBindDateTime`, `DataBindQueryStatus`,
`DataBindQueryLimits`, and `DataBindQueryDiagnostic`; callers do not include
`datetime_parser.h` or `query_vm.h`.

SaltsUtils owns the format-neutral conversion core and the JSON, YAML, CSV, XML
and temporal adapters. Concrete parser and QueryVM types remain above the internal
core boundary; format providers expose bounded CSerde readers and own their native
path-query translation without a registry or fallback path.

Applications consume the complete DataBind component through `Salts::DataBind`.
SaltsUtils encapsulates the schema/dynamic-binding, incremental-stream and adapter
implementation dependencies; consumers do not link internal DataBind targets.

JSON/CSV 文档、YAML 文档与选择结果、XML 文档与节点列表均由各自 Salts parser 创建和释放。
DataBind 只保留转换后的领域值；流式 XML 的增量词法解析属于 `Salts::XmlParser`，
不再由 DataBind 自行实现。流式回调、取消、预算和错误后的资源清理仍由现有回归测试约束。
既有 `turbo_parser.data_bind.*.v1` CMeta 语义标识不是 parser API，保持不变以避免破坏类型身份。

DataBind 3.0 defines two strongly typed routes:

1. schema 生成 `.h/.c`，自动 bind、序列化和反序列化。
2. schema 映射现有 C struct，通过 canonical `cmeta_data_desc` 声明结构与生命周期，
   由 MessagePlan 绑定逻辑字段、FormatPlan 处理外部名称与格式能力。

动态 `DataBindObject` / `DataBindValue` 是显式的宿主程序集成与 runtime-schema 路线，
不是第三套 schema 契约，也不是原生路线的格式中间层。生成式文本路径通过格式 provider
提供的 CSerde tokens、MessagePlan 与 canonical native CMeta graph 进行绑定，
不分配动态 owning root。字段结构和语义类型以 CMeta 为准；
wire layout、外部名称、presence、defaults、validation 与 fingerprint 只存在于 schema overlay。

### 公开 API 分层

- 新生成代码：使用 schema 生成的 `Type_from_*` / `Type_to_*`；文本实现通过
  MessagePlan、FormatPlan 与格式 provider 读写原生对象。
- 已有 C struct：提供 canonical `cmeta_data_desc` 和 `DataBindNativeTypeBinding`，
  编译 MessagePlan 与对应 FormatPlan，使用原生 token decode/encode。
- 未知 schema、脚本与插件宿主：使用 owning `DataBindObject` 或动态
  `DataBindValue`，数值读取优先使用返回 `DataBindStatus` 的 `get_*`。
- 大批量输入：使用 `DataBindStreamConfig` + `data_bind_stream_create()`。
- schema 工具：使用 reflection API；普通业务代码不依赖 reflection 数据结构。

这里的所有权不可混用：`DataBindObject` / `DataBindValue` / `DataBindRecord` 的 owning
dynamic object 由 DataBind 创建，并用对应 DataBind release API 释放；existing/generated
typed struct 的 storage 始终由调用方拥有。原生 staging 必须为空且不含存活资源；
解码成功并关闭 reader 后才能发布，失败时保留原对象。释放由同一 CMeta 生命周期负责；
生成代码通过 `Type_init()` / `Type_clear()` 管理对象，清理不依赖字段 presence 位。

MessagePlan 的原生 list 接纳具有 canonical storage 描述的 builtin 元素或已准入的嵌套 Struct。
builtin 元素必须与 IDL 描述具有相同 CMeta 语义身份，包括整数宽度和符号；
宿主布局相同不足以准入。元素存储、复制与释放由 CSTL/provider collector 管理，
继续受 native item/owned-byte 预算和 schema `@Size` 约束。DataBind presence/null
overlay 位不属于 CMeta 值图；调用方发布带状态位的 staging 时也须转移这些位。
string/bytes 元素所需的独立 owned-buffer 契约尚未纳入 list 准入。

字符串格式参数、格式专用 stream 构造器、`DataBindRecord` facade 和 `as_*`
便捷读取函数作为源码兼容入口保留。新代码使用 `DataBindFormat`、配置式 stream 和
带状态的 getter，以获得可区分的错误语义。

### 路径查询与 `Salts::QueryVM`

DataBind 已通过已安装 parser 的公开查询前端间接复用 `Salts::QueryVM`：JSON 使用
JSONPath，YAML 使用 YPath，CSV 使用 DSV filter，XML 使用 XPath。各前端保留自己的
语法、树遍历、类型转换和操作符语义，并把可执行表达式降低为 QVM bytecode；DataBind
只负责选择、绑定、所有权和错误转换，不直接构造或执行 QVM 指令。

这个边界避免在 DataBind 中再维护一套不完整的“通用路径”语义。现有 parser
前端提供指令、operand、正则和执行步数预算；`DataBindStreamConfig.query_limits`
只转发这些预算。Native QueryVM status 在 implementation boundary 被映射为
`DataBindQueryStatus`，例如 resource limit 映射为
`DATA_BIND_QUERY_RESOURCE_LIMIT`，对调用方不暴露 `QVM_STATUS_*`。
`data_bind_stream_query_diagnostic()` 可复制最后一次 VM 指令、opcode、operand 和消息。
预算在 stream 创建/设置时复制，诊断由 stream 持有；不存在进程全局 DataBind 策略，
也不允许 DataBind 绕过前端直接构造或执行 QVM 指令。

## DataBind IDL Service Contract

DataBind IDL 的 Service contract 只声明逻辑语义：

```text
message GetUserRequest {
  uint64 id;
  optional string expand default "summary";
  string authorization;
}

message GetUserResponse {
  string name;
}

message NotFoundError {
  string resource;
}

service UserService {
  GetUser: GetUserRequest -> GetUserResponse throws NotFoundError;
}
```

Service 的 canonical 事实只有：

```text
Operation: Request -> Response [throws Error...]
```

HTTP/RPC method、route、path/query/header/cookie/body placement、wire method
等都不是 canonical IDL 语义。SaltsUtils 4.0 通过外部 projection config 编译这些
事实，并生成 immutable `FormatPlan` / `TransportPlan` / `MethodPlan`。

例如 HTTP projection config 可以概念化为：

```json
{
  "operations": [{
    "service": "UserService",
    "operation": "GetUser",
    "method": "GET",
    "route": "/users/{id}",
    "ingress_format": "json",
    "egress_format": "json"
  }],
  "fields": [
    {"service":"UserService","operation":"GetUser","direction":"ingress",
     "field":"id","location":"path","wire_name":"id"},
    {"service":"UserService","operation":"GetUser","direction":"ingress",
     "field":"authorization","location":"header","wire_name":"Authorization"}
  ]
}
```

旧的 `[GET(...)]`、`[rpc]`、`[path]`、`[query]`、`[header]`、
`[cookie]`、`[body]` canonical IDL 写法在 4.0 中明确拒绝；不存在兼容
alias 或 fallback。transport mapping 只能从 projection config / generated
plan 获得。

Service reflection 通过 `DataBindService` / `DataBindServiceOperation` 和
`data_bind_service_*` API 暴露逻辑 contract。所有字符串均借用 immutable codec
schema tree，有效期到 `data_bind_free()`；结构体保持 size-versioned prefix
规则。canonical field reflection 同样不包含 transport placement。

这一层只描述 IDL/wire contract：

- DataBind 不绑定 C implementation symbol；
- CMeta 仍是 native type/function semantics 的事实源；
- FormatPlan 描述 representability；
- TransportPlan/MethodPlan 描述 HTTP/RPC 等 projection；
- callable/function adapter、CFlow graph、scheduler、retry/cache/auth policy 都属于后续
  binding/execution 层；
- CHTTP/CRPC 只消费 producer-generated plan，不成为 DataBind runtime dependency。

后续编译 BindingPlan/MethodPlan 时使用同一份 Service Contract 与 CMeta
`cmeta_function_desc` 做 compatibility join；请求 hot path 不重新解释 schema AST。

## Schema 注解标准

字段映射通过 schema 注解定义：

```text
schema Trading [name("trading.v1")];

message Order {
  [name("orderId"), alias("id"), alias("order_id"), c(order_id)] 
  uint64 id;
  [name("symbol")] string symbol;
}
```

- `[name("orderId")]`：规范外部名称；序列化始终输出它。
- `[alias("id")]`：仅用于反序列化输入，可重复。
- `[c(order_id)]`：生成代码中的 C 成员名；现有 struct descriptor 显式写成员。

### 注解换行约定

- `[]` 内部的注解采用 token 解析，空白与换行可任意分隔：和示例里写在同一行、或按可读性拆成多行都有效。
- 建议把属性按一条语句内保持紧凑，字段与类型在同一行；但如属性较长，按换行排版以提升可读性。

```text
message Order {
  [name("orderId"), alias("id"),alias("order_id"),c(order_id)]
  uint64 id;
}
```

动态对象的 `data_bind_object_serialize_*()` 与生成代码的文本输出入口均应用映射。
手工绑定原生 struct 时，将 FormatPlan 的 canonical writer 传给 MessagePlan，
由前者将 canonical 字段名转换为外部名称；alias 仅用于输入。

## 路线一：生成 `.h/.c`

```powershell
salts-idlc order.schema --lang c `
  --output generated/order.h `
  --source-output generated/order.c
```

生成头提供 owning struct 和类型化入口：

```c
#include "order.h"

DataBind *codec = NULL;
DataBindError error = DATA_BIND_ERROR_INIT;
Order_t order;
char *json = NULL;
size_t json_len = 0;

Order_init(&order);
if (Trading_codec_create(&codec, &error) != DATA_BIND_OK) return 1;
if (Order_from_json(codec, &order, input, input_len, &error) != DATA_BIND_OK) return 1;

printf("%llu\n", (unsigned long long)order.order_id);

if (Order_to_json(codec, &order, &json, &json_len, &error) != DATA_BIND_OK) return 1;
data_bind_serialized_free(json);
Order_clear(&order);
data_bind_free(codec);
```

### 编译静态库

```cmake
add_library(order_schema STATIC generated/order.c)
target_include_directories(order_schema PUBLIC generated)
target_link_libraries(order_schema PUBLIC Salts::DataBind)
```

### 编译动态库

```cmake
add_library(order_schema SHARED generated/order.c)
target_compile_definitions(order_schema PRIVATE TBE_GENERATED_BUILD_SHARED)
target_include_directories(order_schema PUBLIC generated)
target_link_libraries(order_schema PUBLIC Salts::DataBind)

target_compile_definitions(my_app PRIVATE TBE_GENERATED_USE_SHARED) # Windows consumer
target_link_libraries(my_app PRIVATE order_schema)
```

Linux/macOS shared library 构建也定义 `TBE_GENERATED_BUILD_SHARED`，生成头会设置默认
symbol visibility。静态库不定义这两个宏。

## 路线二：映射现有 C struct

这条路线不运行 `salts-idlc`，也不生成业务头文件。完整可运行示例见
[`benchmark_data_bind_native.c`](benchmark_data_bind_native.c)，nullable 状态与释放回归见
[`test_data_bind_native_nullable.c`](test_data_bind_native_nullable.c)，标量类型身份、数值边界
与 UUID 生命周期见
[`native_scalar_contract_test.c`](../tests/native_storage/native_scalar_contract_test.c)。

raw storage 的 `data_bind_native_init/clear` 会先验证完整 CMeta 图，再接触 owning 字段。
字段重叠、offset 溢出、嵌套非法布局或预算不足不会改写对象，也不会释放现有 owner。
成功 clear 由 provider 释放每个 owning 字段，并清零结构中未反射的 overlay 字节；可以
重复 clear 或重新赋值后重用。相邻 owner、非法布局的读写准入及失败后重试见
[`native_ownership_boundary_test.c`](../tests/native_storage/native_ownership_boundary_test.c)。

native 生命周期复用 CMeta 的 `cmeta_data_value_*` 接口：叶子值与 Map 的 semantic-zero
判断由 CMeta 负责，DataBind 不维护另一份按原生标量类型展开的零值表。DataBind 只补充
自身的结构存储 envelope 检查，以及固定集合元素中的 overlay 检查。清理在 CMeta
释放资源后，沿反射字段递归复位内联子 Struct 的未反射字节；不会再次调用子值的释放
回调，也不会用整体清零覆盖 provider 定义的 semantic zero。该操作要求对象与 workspace
由本次同步调用独占，沿用已准入图的深度限制，不增加分配、缓存或所有权状态。

1. 用 CMeta layout 和 `cmeta_data_desc` 声明原生成员。拥有字符串的 `tstr` 使用
   `cmeta_tstr_cmeta_data`；该 provider 负责初始化、移动与释放。图中保留 canonical
   字段名，schema 的 name/alias 等格式属性由 overlay 表达。
2. 加载 schema，使用 `DataBindNativeTypeBinding` 编译 MessagePlan，再为所选格式
   编译 FormatPlan。plans 借用的 codec 与 native metadata 必须存活到 plans 释放后。
3. 配置对齐的 workspace、最大深度、项目数量与 owned bytes 预算。workspace 由一次
   调用独占；并发调用需要各自的 workspace、对象和 reader/writer lease。
4. 输入经格式 provider 和 canonical reader 进入 MessagePlan 的 fresh staging。
   成功后关闭 reader，再释放旧对象并用 CMeta move 发布 staging；失败则清理 staging，
   原对象保持不变。provider 的 borrowed token view 不可跨 reader close 保留。
5. 输出通过 canonical writer 与格式 provider 写入调用方拥有的 sink。完成 canonical
   writer 后关闭 provider writer；两者的错误都必须检查。容量不足返回错误，已写出的
   前缀不能视为完整文档；调用方丢弃它后可使用新的 lease 重试。
6. 统一 cleanup 通过 CMeta 生命周期释放对象，再释放 FormatPlan、MessagePlan 和 codec。
   不按 presence 位跳过已拥有的字符串，也不以清零内存代替释放。

原生 flags 可读取整数、单个名称以及组合它们的数组，嵌套数组按位合并，空数组表示零。
数组和成员均计入 `max_items`，实际嵌套计入 `max_depth`；只有完整输入与 CMeta domain
校验成功后才调用赋值 provider。未知名称、非法位、截断或预算耗尽不会发布部分值。
普通 enum 仍拒绝数组。输出保持 canonical 整数位值，不把 flags 数组作为另一套宿主存储。

JSON 的根 bytes 字段沿用 UTF-8 文本表示：显式
`data_bind_message_plan_decode_native_format(..., DATA_BIND_FORMAT_JSON, ...)` 将
STRING token 的完整长度复制到 provider-owned byte buffer，FormatPlan canonical writer
把根 BYTES token 投影为 JSON 字符串。嵌入 NUL 会转义并保留；非法 UTF-8 输出失败，
不改变源对象所有权。普通 native decoder、Binary、YAML 和嵌套 bytes 不执行此投影。
该协议及容量不足后的重试见
[`native_bytes_json_test.c`](../tests/native_storage/native_bytes_json_test.c)。

公共契约见 [`data_bind_message_plan.h`](data_bind_message_plan.h)、
[`data_bind_projection_plan.h`](data_bind_projection_plan.h) 和
[`data_bind_format_provider.h`](data_bind_format_provider.h)。完整 FormatPlan 编译用于输出
能力准入；不支持的逻辑状态必须显式失败，不能隐式降级为其他格式或旧 typed 路径。

benchmark 的计划编译与正确性检查位于计时外。每个 decode 样本包含 provider 的开关、
staging 解码、旧值释放与移动发布；每个 encode 样本包含 provider 的开关及有界 sink
写入。字节数按单份 JSON 文档计算，不含结尾 NUL。此口径不同于旧版分配输出字符串的
benchmark，结果不能直接作为迁移前后的性能比较。

## RulesForge/TurboScript 如何使用

### 动态运行时模式

适合规则字段、脚本对象和运行时才知道的 schema：

```text
schema text/file
    -> data_bind_create[_from_text]()
    -> DataBindObject
    -> immutable get/validate/serialize
```

部署只需要 schema 和 DataBind 静态/动态库。先用
`data_bind_object_value()` 获取 borrowed 根值，再用 `data_bind_value_get()` 访问字段，
无需外置编译器。`DataBindObject` 是不可变 owning handle；需要修改时应重建一个新对象。

### 原生成员模式

需要 `order.id`、IDE 类型检查或固定 ABI 时：

```text
CI/构建阶段:
schema -> salts-idlc -> order.h/order.c -> static/shared schema library

运行阶段:
RulesForge/TurboScript host -> 已编译 schema library -> DataBind runtime
```

C 编译器只出现在 CI、安装或发布阶段。生产进程不执行编译器。动态装载时可通过
生成的 `*_schema_codec()` 获取 `tbe_schema_codec_v1_t` provider，先校验 ABI，
再调用 provider。

## 动态对象示例

```c
DataBind *codec = NULL;
DataBindObject *order = NULL;
DataBindError error = DATA_BIND_ERROR_INIT;
char *output = NULL;
size_t output_len = 0;

if (data_bind_create("order.schema", &codec, &error) != DATA_BIND_OK) return 1;
if (data_bind_object_from_json(codec, "Order", input, input_len,
                               &order, &error) != DATA_BIND_OK) return 1;
if (data_bind_object_serialize_json(codec, order, &output,
                                    &output_len, &error) != DATA_BIND_OK) return 1;

data_bind_serialized_free(output);
data_bind_object_free(order);
data_bind_free(codec);
```

动态对象不能写成 C 表达式 `order.id`，因为 C 编译器不知道运行时 schema。宿主
语言应把 `order.id` 语法转换为动态属性查询；若必须得到真实 C 成员，只能选择
上述两条强类型路线之一。

## CMeta / CFlow / Reactive 可选适配

解析、序列化以及将不可变动态值接入 CMeta range、CFlow Stream/Publisher，均链接
`Salts::DataBind`；所需的 Salts 基础依赖由该目标传递：

```cmake
find_package(SaltsUtils CONFIG REQUIRED
  PATHS "$ENV{SALTS_UTILS_ROOT}" NO_DEFAULT_PATH)
target_link_libraries(my_app PRIVATE Salts::DataBind)
```

LIST/SET 映射为 `DataBindValueRef`，OBJECT 映射为
`DataBindFieldRef`，MAP 映射为 `DataBindMapEntryRef`。三者均有稳定的 CMeta type
identity，并保持 DataBind 的 encounter/schema order。

动态对象的 JSON/YAML、XML、CSV 输出与公开 CMeta adapter 共用内部 borrowed range 实现，
通过同一套字段、元素和 map entry 访问器读取 CSTL 存储；这些实现位于同一运行时库。
遍历状态位于栈上，不分配或保留 payload；owner 在整个序列化调用中必须保持存活且不可变，
generation 检查不提供并发访问保证。单次子节点访问为 O(1)，遍历为 O(节点数)，递归栈
受既有深度上限约束；名称映射、DOM 构造和文本输出仍有各自的分配成本。
各格式保留自己的标量表示、XML 重复字段和 CSV 路径/空集合规则。文本输出不借用
`data_bind_value_reader` 的 CSerde 标量转换，因为 UUID、日期、金额等文本语义不同。

```c
#include "data_bind_cflow.h"

cflow_publisher source = {0};
const DataBindValue *items = data_bind_value_get(root, "items");

if (data_bind_cflow_publisher_from_value(
        items, DATA_BIND_CMETA_RANGE_VALUES, &source) != DATA_BIND_OK) {
  return 1;
}
/* cflow_subscribe() 成功后移动 source；按 Subscription demand 拉取。 */
```

range、stream、publisher 和 subscription 都只借用 `DataBindValue` owner；适配器不
释放 owner，也不预取或缓存 payload。owner 必须存活至 range 遍历完成、stream 销毁，
或 publisher/subscription 关闭。释放 owner 后，已发出的 value/name/key 指针立即
失效。公开的 `data_bind_cmeta_range_init()` 与上述内部 range 均捕获容器 generation；
后续结构性修改会使该 range 的遍历返回 `CMETA_GEN_MUTATED`，而不是继续读取可能已经
移动的槽位。普通 Record/List/Map borrowed view 不携带 generation，也不返回
`CMETA_GEN_MUTATED`；它们按 owner 生命周期与各 accessor 的失效规则使用。SET/MAP 始终遍历 owning
ordered Vec：SET 是首次语义插入顺序，MAP 是插入/wire 顺序，绝不暴露 HashSet/HashMap
bucket 顺序。range cursor 是单线程对象；Reactive resume 由 CFlow 串行化，cancel/close
只结束消费状态。kind 与值类型不匹配时返回 `DATA_BIND_ERR_INVALID_ARG`，不自动降级。

## 流式消费

stream 默认保留所有绑定结果，并只在 `data_bind_stream_finish()` 成功时转移
`out_value` 所有权。新代码使用一次性验证配置的构造入口；只需逐条处理时，
callback-only 不要求虚设 `out_value`：

```c
DataBindStreamConfig config = DATA_BIND_STREAM_CONFIG_INIT;
data_bind_stream_t *stream = NULL;
config.format = DATA_BIND_FORMAT_JSON;
config.selection = DATA_BIND_STREAM_SELECT_PATH_ALL;
config.type_name = "Order";
config.path = "$.orders[*]";
config.output_mode = DATA_BIND_STREAM_OUTPUT_CALLBACK_ONLY;
config.record_callback = consume_order;
config.record_callback_user = context;
config.query_limits.max_steps = 100000;

if (data_bind_stream_create(codec, &config, &stream, &error) != DATA_BIND_OK ||
    data_bind_stream_feed(stream, input, input_len) != DATA_BIND_OK ||
    data_bind_stream_finish(stream) != DATA_BIND_OK) {
  data_bind_stream_destroy(stream);
  return 1;
}

data_bind_stream_destroy(stream);
```

回调收到的 `DataBindValue` 只在本次同步回调期间有效，不得跨回调、`feed()` 或
`finish()` 保存其裸指针。可增量处理的 JSON、CSV、XML 记录在回调返回后立即释放；
YAML 和不可流式的路径表达式仍需缓存输入并在 `finish()` 绑定，只是不再保留最终
结果。stream handle 为单线程对象。`DATA_BIND_RECORD_STOP` 只停止后续回调，仍完成
语法校验和结果计数；`DATA_BIND_RECORD_CANCEL` 或 `data_bind_stream_cancel()` 立即终止，
后续 `feed()` / `finish()` 返回 `DATA_BIND_ERR_CANCELED`。任何失败或取消都不转移
final output。

## 线程与生命周期

- `DataBind` 创建完成后不可变，可由多个线程共享；每个调用必须使用独立的输出对象和
  `DataBindError`，且释放 codec 前必须确保所有调用已经结束。
- 已发布的 owning dynamic root 保留不可变的语义 metadata，可在创建它的
  `DataBind *codec` 释放后继续读取、clone 和释放；需要 schema overlay 的后续操作仍须
  传入匹配 codec。
- owning object 使用 `data_bind_object_free()`；独立 owning value 使用
  `data_bind_value_free()`；生成的 native 对象使用对应 `*_clear()`。三者均通过
  当前 CMeta 生命周期释放，不使用历史 TBE typed 释放入口。
- accessor 返回的 child/string 指针以及 range/view 都借用 owning root；root 释放后其
  所有 descendants/views 立即失效。
- `data_bind_value_clone()` 创建独立 owning storage；源与 clone 可分别释放。不可变的
  semantic type graph 可以通过 retained reference 安全共享，不共享可变容器槽位。
- typed object 在首次使用前调用 `*_init()`，结束时调用 `*_clear()`。
- 外部 schema 和输入必须在可信边界校验；解析错误直接返回，不自动修复或降级。

## 错误与 ABI

公开 API 返回 `DataBindStatus`，详细上下文写入 `DataBindError`。应用边界应记录
status、path 和 message，并停止使用失败输出。

```c
if (data_bind_abi_version() != DATA_BIND_ABI_VERSION) {
  /* 拒绝加载不匹配的运行时库。 */
}
```

DataBind 3.0 的 C ABI 版本为 10。历史 typed runtime 和动态 kind 到 CMeta kind
的迁移接口已删除，旧调用方必须迁移到 canonical native/schema API 后重新编译。
Binary 计划要求 ABI 2 和完整的当前记录；旧 provider 必须重新生成，不提供 adapter。
详细所有权与生成库边界见
[`RECORD_ABI.md`](RECORD_ABI.md)。


## Canonical BindingPlan

Service/native binding follows the canonical #141 split:

```text
DataBind IDL Service Contract
        +
CMeta type/function reflection
        ↓
DataBind BindingPlan
```

`DataBindBindingPlan` is format-neutral and transport-runtime-neutral. It does not require
TBE wire descriptors and does not expose a closed HTTP/RPC protocol enum.

Transport-specific IDL facts compile into generic logical addresses:

```text
VALUE / METADATA / PAYLOAD / PART / RESULT / ERROR
        +
provider-owned space/name/ordinal
```

For example, HTTP query/header bindings may compile to `VALUE:http.query` and
`METADATA:http.header`; RPC parameters compile to `VALUE:rpc.param`.
Future FlowMQ/Flowie/semantic-record adapters consume the same ABI by defining their own
admitted spaces rather than extending DataBind core with transport runtime state.

CMeta remains authoritative for native type/function semantics. DataBind owns only the
IDL-to-native projection, defaults/optional presence, logical operation identity, and the
immutable compiled plan. Invocation remains exact-ABI generated code or another admitted
execution adapter.
