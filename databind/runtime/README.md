# DataBind 3.0

DataBind 是 SaltsUtils 的组成部分，源码、构建、测试、安装和发布均由 SaltsUtils 负责。
消费者通过 `find_package(SaltsUtils)` 使用唯一公开目标 `Salts::DataBind`，
不使用独立 DataBind package/root，也不组装内部目标或补造兼容 alias。
生成代码、现有原生 C struct 与动态对象均通过 DataBind 绑定；不存在
DataBind 私有的 owning dynamic-container compatibility engine、storage fallback、第二 binder
或格式 fallback。原生对象使用 canonical CMeta graph 与 DataBind plans；历史
`TBE_TYPED_*` runtime 的移除由 [#488](https://github.com/qigao/salts-utils/issues/488) 跟踪。

DataBind 是 SaltsUtils 中的 schema 驱动纯 C 运行时。它解析 schema、构造动态值、校验字段，
并统一处理 TBE binary、JSON、YAML、XML 和 CSV。它不加载或生成运行时代码，
运行时也不要求 C/C++ 编译器。

`salts-idlc` 与 DataBind 是两个不同层次：

- `salts-idlc`：构建期工具，把 schema 渲染为 `.h/.c`。
- `DataBind`：运行时库，为动态对象、现有 C struct 映射和生成代码提供公共
  bind/serialization 引擎。

## 设计边界

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

1. 用 CMeta layout 和 `cmeta_data_desc` 声明原生成员。拥有字符串的 `tstr` 使用
   `salts_tstr_cmeta_data`；该 provider 负责初始化、移动与释放。图中保留 canonical
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

通过 SaltsUtils 的唯一公开目标 `Salts::DataBind` 使用 DataBind，包括将已解析的
不可变动态值接入 CMeta 或 CFlow 的适配 API。内部依赖由 SaltsUtils 封装：

```cmake
find_package(SaltsUtils CONFIG REQUIRED
  PATHS "$ENV{SALTS_UTILS_ROOT}" NO_DEFAULT_PATH)
target_link_libraries(my_app PRIVATE Salts::DataBind)
```

消费者不直接链接内部 CMeta/CFlow 适配目标。LIST/SET 映射为 `DataBindValueRef`，OBJECT 映射为
`DataBindFieldRef`，MAP 映射为 `DataBindMapEntryRef`。三者均有稳定的 CMeta type
identity，并保持 DataBind 的 encounter/schema order。

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
失效。只有由 `data_bind_cmeta_range_init()` 创建的 `cmeta_range` 捕获容器 generation；
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
- owning object/value 必须使用对应的 DataBind/TBE typed 释放函数。
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

DataBind 3.0 的 ABI 版本为 9。2.3 将 stream 的 `feed`、`feed_file`、`finish`
声明统一为 `DataBindStatus`；导出符号和调用约定未变，但保存这些函数指针的调用方
应使用新签名重新编译。2.4 在枚举尾部增加 buffer-too-small/canceled 状态，并增加
版本化 descriptor、统一 format 和配置式 stream 入口。2.5 在版本化
`DataBindStreamConfig` 尾部增加 query budgets，并增加 setter/diagnostic accessor；旧尺寸
配置继续使用原行为，既有符号保留。
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
