# TurboParser DataBind 2.5

DataBind 是独立的 schema 驱动纯 C 运行时。它解析 schema、构造动态值、校验字段，
并统一处理 TBE binary、JSON、YAML、XML 和 CSV。它不加载或生成运行时代码，
运行时也不要求 C/C++ 编译器。

`tbe_compiler` 与 DataBind 是两个不同层次：

- `tbe_compiler`：构建期工具，把 schema 渲染为 `.h/.c`。
- `DataBind`：运行时库，为动态对象、现有 C struct 映射和生成代码提供公共
  bind/serialization 引擎。

## 设计边界

DataBind 是 TurboParser 自有的 schema、动态值和 typed conversion 运行时。它只依赖
TurboParser 的 schema 与具体格式解析能力，不提供跨包 binding kernel 的桥接或生成路径。

DataBind 2.5 只定义两条强类型路线：

1. schema 生成 `.h/.c`，自动 bind、序列化和反序列化。
2. schema 映射现有 C struct，通过 `TBE_TYPED_*` 宏声明 descriptor，自动
   bind、序列化和反序列化。

动态 `DataBindObject` 是两条路线共用的格式中间层和宿主程序集成入口，不是
第三套 schema 契约。schema 始终是字段类型、wire layout 和外部名称的唯一事实源。

### 公开 API 分层

- 新生成代码：使用 schema 生成的 `Type_from_*` / `Type_to_*`；生成实现通过
  `TbeTypedDescriptor` 校验 descriptor ABI。
- 已有 C struct：使用 `TBE_TYPED_*` descriptor 和 enum-based
  `TBE_TYPED_BIND_PARSE_EX` / `TBE_TYPED_BIND_SERIALIZE_EX`。
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

### 路径查询与 `Rocida::QueryVM`

DataBind 已通过已安装 parser 的公开查询前端间接复用 `Rocida::QueryVM`：JSON 使用
JSONPath，YAML 使用 YPath，CSV 使用 DSV filter，XML 使用 XPath。各前端保留自己的
语法、树遍历、类型转换和操作符语义，并把可执行表达式降低为 QVM bytecode；DataBind
只负责选择、绑定、所有权和错误转换，不直接构造或执行 QVM 指令。

这个边界避免在 DataBind 中再维护一套不完整的“通用路径”语义。2.5 由四个 parser
前端统一提供指令、operand、正则和执行步数预算；`DataBindStreamConfig.query_limits`
只转发这些预算，并把 `TURBO_QUERY_RESOURCE_LIMIT` 映射为 `DATA_BIND_ERR_LIMIT`。
`data_bind_stream_query_diagnostic()` 可复制最后一次 VM 指令、opcode、operand 和消息。
预算在 stream 创建/设置时复制，诊断由 stream 持有；不存在进程全局 DataBind 策略，
也不允许 DataBind 绕过前端直接构造或执行 QVM 指令。

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

调用普通 `data_bind_object_serialize_*()` 或 `tbe_typed_serialize()` 即应用映射，
不再存在单独的 `_mapped` 序列化入口。

## 路线一：生成 `.h/.c`

```powershell
tbe_compiler order.schema --lang c `
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
tbe_typed_serialized_free(json);
Order_clear(&order);
data_bind_free(codec);
```

### 编译静态库

```cmake
add_library(order_schema STATIC generated/order.c)
target_include_directories(order_schema PUBLIC generated)
target_link_libraries(order_schema PUBLIC TurboParser::DataBind)
```

### 编译动态库

```cmake
add_library(order_schema SHARED generated/order.c)
target_compile_definitions(order_schema PRIVATE TBE_GENERATED_BUILD_SHARED)
target_include_directories(order_schema PUBLIC generated)
target_link_libraries(order_schema PUBLIC TurboParser::DataBind)

target_compile_definitions(my_app PRIVATE TBE_GENERATED_USE_SHARED) # Windows consumer
target_link_libraries(my_app PRIVATE order_schema)
```

Linux/macOS shared library 构建也定义 `TBE_GENERATED_BUILD_SHARED`，生成头会设置默认
symbol visibility。静态库不定义这两个宏。

## 路线二：映射现有 C struct

```c
#include "tbe_typed.h"

typedef struct Order {
  uint64_t order_id;
  tstr symbol;
} Order;

TBE_TYPED_DEFINE_STRUCT(
    ORDER_BINDING, Order, "Order",
    TBE_TYPED_FIELD(Order, order_id, "id", TBE_TYPED_U64, TBE_TYPED_REQUIRED),
    TBE_TYPED_FIELD(Order, symbol, "symbol", TBE_TYPED_STRING, TBE_TYPED_REQUIRED));
```

应用先加载同一 schema，再验证 descriptor 并操作对象：

```c
DataBind *codec = NULL;
DataBindError error = DATA_BIND_ERROR_INIT;
Order order;
char *json = NULL;

if (data_bind_create("order.schema", &codec, &error) != DATA_BIND_OK) return 1;
if (tbe_typed_validate_schema(codec, "Order", &ORDER_BINDING, &error) != DATA_BIND_OK) return 1;
if (TBE_TYPED_BIND_INIT(ORDER_BINDING, &order, &error) != DATA_BIND_OK) return 1;
if (TBE_TYPED_BIND_PARSE_EX(codec, ORDER_BINDING, DATA_BIND_FORMAT_JSON,
                            input, input_len, 0, &order, &error) != DATA_BIND_OK) return 1;

printf("%llu\n", (unsigned long long)order.order_id);

if (TBE_TYPED_BIND_SERIALIZE_EX(codec, ORDER_BINDING, &order,
                                DATA_BIND_FORMAT_JSON, &json, NULL,
                                &error) != DATA_BIND_OK) return 1;
tbe_typed_serialized_free(json);
TBE_TYPED_BIND_CLEAR(ORDER_BINDING, &order);
data_bind_free(codec);
```

这条路线不运行 `tbe_compiler`，也不生成业务头文件。宏 descriptor 将
`offsetof()`、成员类型、可选位和 wire 属性固化进普通 C 常量。

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
schema -> tbe_compiler -> order.h/order.c -> static/shared schema library

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

DataBind 核心 target 的依赖与 ABI 保持不变。需要把已解析的不可变动态值接入 CMeta
或 CFlow 时，显式链接可选适配库：

```cmake
target_link_libraries(my_app PRIVATE TurboParser::DataBindCFlow)
```

`TurboParser::DataBindCFlow` 传递链接 `TurboParser::DataBindCMeta`；只需要同步
`cmeta_range` 时可单独链接后者。LIST/SET 映射为 `DataBindValueRef`，OBJECT 映射为
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
失效。range cursor 是单线程对象；Reactive resume 由 CFlow 串行化，cancel/close 只
结束消费状态。kind 与值类型不匹配时返回 `DATA_BIND_ERR_INVALID_ARG`，不自动降级。

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
- owning object/value 必须使用对应的 DataBind/TBE typed 释放函数。
- view 借用 owning record/object，owner 释放或清空后立即失效。
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

DataBind 2.5 的 ABI 版本为 8。2.3 将 stream 的 `feed`、`feed_file`、`finish`
声明统一为 `DataBindStatus`；导出符号和调用约定未变，但保存这些函数指针的调用方
应使用新签名重新编译。2.4 在枚举尾部增加 buffer-too-small/canceled 状态，并增加
版本化 descriptor、统一 format 和配置式 stream 入口。2.5 在版本化
`DataBindStreamConfig` 尾部增加 query budgets，并增加 setter/diagnostic accessor；旧尺寸
配置继续使用原行为，既有符号保留。
详细所有权与生成库边界见
[`RECORD_ABI.md`](RECORD_ABI.md)。
