# DataBind 3.0

DataBind 是 SaltsUtils 的组成部分，源码、构建、测试、安装和发布均由 SaltsUtils 负责。
消费者通过 `find_package(SaltsUtils)` 使用唯一公开目标 `Salts::DataBind`，
不使用独立 DataBind package/root，也不组装内部目标或补造兼容 alias。
生成代码、现有原生 C struct 与动态对象均通过 DataBind 绑定；不存在
DataBind 私有的 owning dynamic-container compatibility engine、storage fallback、第二 binder
或格式 fallback。仍受支持的 `DATA_BIND_TYPED_*` raw typed 路线直接绑定调用方拥有的 C struct，
它是下文所述的独立 typed API，不是动态容器兼容引擎或 fallback。

DataBind 是 SaltsUtils 中的 schema 驱动纯 C 运行时。它解析 schema、构造动态值、校验字段，
并统一处理 TBE binary、JSON、YAML、XML 和 CSV。它不加载或生成运行时代码，
运行时也不要求 C/C++ 编译器。

`databindc` 与 DataBind 是两个不同层次：

- `databindc`：构建期工具，把 schema 渲染为 `.h/.c`。
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
2. schema 映射现有 C struct，通过 `DATA_BIND_TYPED_*` 宏声明 raw typed metadata，自动
   bind、序列化和反序列化。

动态 `DataBindObject` / `DataBindValue` 是显式的宿主程序集成与 runtime-schema 路线，
不是第三套 schema 契约，也不是原生 descriptor 路线的格式中间层。受支持的原生
descriptor JSON/YAML/CSV/XML 路径直接在格式 AST/DOM、canonical native CMeta graph 与
schema overlay 之间转换，不分配动态 owning root。字段结构和语义类型以 CMeta 为准；
wire layout、外部名称、presence、defaults、validation 与 fingerprint 只存在于 schema overlay。

### 公开 API 分层

- 新生成代码：使用 schema 生成的 `Type_from_*` / `Type_to_*`；生成实现通过
  `DataBindTypedDescriptor` 校验 descriptor ABI。
- 已有 C struct：deferred storage 使用 `DATA_BIND_TYPED_*` raw metadata 和 enum-based
  `DATA_BIND_TYPED_BIND_PARSE_EX` / `DATA_BIND_TYPED_BIND_SERIALIZE_EX`。
- ABI-v2 native descriptor：调用方必须同时提供显式 schema overlay 与通过校验的
  canonical CMeta graph；graphless descriptor 与 ABI-v1 一律返回 schema error。
- 未知 schema、脚本与插件宿主：使用 owning `DataBindObject` 或动态
  `DataBindValue`，数值读取优先使用返回 `DataBindStatus` 的 `get_*`。
- 大批量输入：使用 `DataBindStreamConfig` + `data_bind_stream_create()`。
- schema 工具：使用 reflection API；普通业务代码不依赖 reflection 数据结构。

这里的所有权不可混用：`DataBindObject` / `DataBindValue` / `DataBindRecord` 的 owning
dynamic object 由 DataBind 创建，并用对应 DataBind release API 释放；existing/generated
typed struct 的 storage 始终由调用方拥有。typed destination 必须先按 descriptor 协议
（生成代码即 `Type_init()`）初始化至 semantic zero，并在成功或失败后的统一 cleanup 中按
同一 descriptor 协议（生成代码即 `Type_clear()`）清理。

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

DataBind IDL 可以在同一份数据类型定义中声明 transport-neutral service contract：

```text
message GetUserRequest {
  [path] uint64 id;
  optional [query] string expand default "summary";
  [header("Authorization")] string authorization;
}

message GetUserResponse {
  string name;
}

message NotFoundError {
  string resource;
}

service UserService {
  [GET("/users/{id}"), rpc]
  GetUser: GetUserRequest -> GetUserResponse throws NotFoundError;
}
```

Service 的逻辑事实只有：

```text
Operation: Request -> Response [throws Error...]
```

`GET/POST/.../rpc` 是 transport projection，不是 Service 本身。字段复用既有
attribute 机制表达 `path/query/header/cookie/body`；bare attribute 默认使用字段名，
只有外部名称不同才显式写名字，例如 `[header("X-Request-ID")]`。

bare `[rpc]` 的 effective wire name 为 `Service.Operation`；兼容既有协议时可用
`[rpc("legacy.method")]` 覆盖。HTTP route 采用 `/users/{id}` 形式，并在 schema
admission 时校验每个 placeholder 与 request message 中恰好一个 required `[path]`
字段双向匹配。

Service reflection 通过 `DataBindService` / `DataBindServiceOperation` 和
`data_bind_service_*` API 暴露。所有字符串均借用 immutable codec schema tree，
有效期到 `data_bind_free()`；结构体保持 size-versioned prefix 规则。

这一层只描述 IDL/wire contract：

- DataBind 不绑定 C implementation symbol；
- CMeta 仍是 native type/function semantics 的事实源；
- callable/function adapter、CFlow graph、scheduler、retry/cache/auth policy 都属于后续
  binding/execution 层；
- CHTTP/CRPC 只消费 projection，不成为 DataBind runtime dependency。

后续编译 binding plan 时使用同一份 Service Contract 与 CMeta
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

调用普通 `data_bind_object_serialize_*()` 或 `data_bind_typed_serialize()` 即应用映射，
不再存在单独的 `_mapped` 序列化入口。

## 路线一：生成 `.h/.c`

```powershell
databindc order.schema --lang c `
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
data_bind_typed_serialized_free(json);
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
target_compile_definitions(order_schema PRIVATE DATABIND_GENERATED_BUILD_SHARED)
target_include_directories(order_schema PUBLIC generated)
target_link_libraries(order_schema PUBLIC Salts::DataBind)

target_compile_definitions(my_app PRIVATE DATABIND_GENERATED_USE_SHARED) # Windows consumer
target_link_libraries(my_app PRIVATE order_schema)
```

Linux/macOS shared library 构建也定义 `DATABIND_GENERATED_BUILD_SHARED`，生成头会设置默认
symbol visibility。静态库不定义这两个宏。

## 路线二：映射现有 C struct

```c
#include "data_bind_typed.h"

typedef struct Order {
  uint64_t order_id;
  tstr symbol;
} Order;

DATA_BIND_TYPED_DEFINE_STRUCT(
    ORDER_BINDING, Order, "Order",
    DATA_BIND_TYPED_FIELD(Order, order_id, "id", DATA_BIND_TYPED_U64, DATA_BIND_TYPED_REQUIRED),
    DATA_BIND_TYPED_FIELD(Order, symbol, "symbol", DATA_BIND_TYPED_STRING, DATA_BIND_TYPED_REQUIRED));
```

应用先加载同一 schema，再验证 descriptor 并操作对象：

```c
DataBind *codec = NULL;
DataBindError error = DATA_BIND_ERROR_INIT;
Order order;
char *json = NULL;

if (data_bind_create("order.schema", &codec, &error) != DATA_BIND_OK) return 1;
if (data_bind_typed_validate_schema(codec, "Order", &ORDER_BINDING, &error) != DATA_BIND_OK) return 1;
if (DATA_BIND_TYPED_BIND_INIT(ORDER_BINDING, &order, &error) != DATA_BIND_OK) return 1;
if (DATA_BIND_TYPED_BIND_PARSE_EX(codec, ORDER_BINDING, DATA_BIND_FORMAT_JSON,
                            input, input_len, 0, &order, &error) != DATA_BIND_OK) return 1;

printf("%llu\n", (unsigned long long)order.order_id);

if (DATA_BIND_TYPED_BIND_SERIALIZE_EX(codec, ORDER_BINDING, &order,
                                DATA_BIND_FORMAT_JSON, &json, NULL,
                                &error) != DATA_BIND_OK) return 1;
data_bind_typed_serialized_free(json);
DATA_BIND_TYPED_BIND_CLEAR(ORDER_BINDING, &order);
data_bind_free(codec);
```

这条路线不运行 `databindc`，也不生成业务头文件。宏生成的 raw typed metadata 将
`offsetof()`、成员类型、可选位和 wire 属性固化进普通 C 常量。

这些宏不会合成 ABI-v2 descriptor。若现有 struct 要进入 CMeta-authoritative
descriptor 路线，必须显式提供 canonical `cmeta_data_desc` 根，并使用
`DATA_BIND_TYPED_DESCRIPTOR_INIT(&overlay, &native_data)`。ABI-v1 或缺少 native graph
直接失败；不会转入 raw 路线。当前自动 descriptor slice 只覆盖固定宽度整数、
F32/F64、带完整 CMeta operations 的非 flags enum，以及非 optional 的嵌套 Struct。

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
schema -> databindc -> order.h/order.c -> static/shared schema library

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
- owning object/value 必须使用对应的 DataBind/DataBind typed 释放函数。
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
