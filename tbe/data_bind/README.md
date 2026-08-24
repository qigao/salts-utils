# TurboUtils DataBind 2.5

DataBind 是独立的 schema 驱动纯 C 运行时。它解析 schema、构造动态值、校验字段，
并统一处理 TBE binary、JSON、YAML、XML 和 CSV。它不加载或生成运行时代码，
运行时也不要求 C/C++ 编译器。

`tbe_compiler` 与 DataBind 是两个不同层次：

- `tbe_compiler`：构建期工具，把 schema 渲染为 `.h/.c`。
- `DataBind`：运行时库，为动态对象、现有 C struct 映射和生成代码提供公共
  bind/serialization 引擎。

## 设计边界

DataBind 不属于 CBind，也不是 CBind/TbeCBind 的 adapter 或 fallback。四条路线按输入与
目标选择：

| 路线 | 使用条件 | 运行时结果 |
| --- | --- | --- |
| `TurboUtils::CBind` | 已有最终 CMeta semantic descriptor 与 CSerde reader | 直接填充已有 C struct |
| `TurboParser::TbeCBind` | TBE schema 运行时到达，且调用方已有 native CMeta descriptor | immutable overlay plan，再直接调用 CBind |
| `TurboParser::DataBind` | 需要动态值树、跨格式对象或 `TBE_TYPED_*` conversion | DataBind-owned dynamic/typed result |
| build-time sidecar | schema 在构建期已知 | 生成 immutable descriptor，direct CBind |

TbeCBind 不调用或链接 DataBind；DataBind 也不是缺少 native shape 时的后备路径。TBE
schema 本身没有目标 ABI 的 size/alignment/offset 与 buffer ownership callbacks，因而
schema-only 不能推出任意 C struct。TbeCBind 的完整选择矩阵、CMake 用法和可编译示例见
[`../tbe_cbind/README.md`](../tbe_cbind/README.md)。

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

字符串格式参数、格式专用 stream 构造器、`DataBindRecord` facade 和 `as_*`
便捷读取函数作为源码兼容入口保留。新代码使用 `DataBindFormat`、配置式 stream 和
带状态的 getter，以获得可区分的错误语义。

### 路径查询与 `parser/query_vm`

DataBind 已通过 parser 的公开查询前端间接复用 `parser/query_vm`：JSON 使用
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

## 可选 CBind semantic sidecar

`--cbind-output` 是生成 owning C record 的额外、显式 opt-in 路径。它生成的不是
另一套 TBE codec：`CSerde` 是 format-neutral 的 token contract，`CBind` 是把该 token
contract 解到 CMeta-described native storage 的 format-neutral kernel，而 JSON 的具体
语法、DOM 与 token 投影仍由 TurboParser JSON adapter 负责。

该 sidecar 是 build-time direct CBind 路线，不经过 TbeCBind plan，也不把 DataBind
变成 CBind 的组成部分。示例 schema library 同时编译 typed source 与 sidecar 时会分别
链接 `TurboParser::DataBind` 和 `TurboUtils::CBind`，这是两个并列产物的依赖并集；仅使用
sidecar descriptor/decode 的执行链仍是 direct CBind。

同一份 TBE schema 是 generated owning struct、`TbeTypedType` 与 CBind semantic
sidecar 的唯一事实源。不过二者的 metadata 保持独立：`TbeTypedType` 独占 TBE 的
wire/layout（offset、endianness、presence bitmap、fixed block 等），sidecar 单独生成
不可变 CMeta semantic descriptor；不可把任一 descriptor 当作另一者的替代品。

```powershell
tbe_compiler order.schema --lang c `
  --output generated/order.h `
  --source-output generated/order.c `
  --cbind-output generated/order_cbind.c
```

`--cbind-output` 只支持内置 C generator，且必须同时指定不同路径的 `--output` 与
`--source-output`。它与 header、typed source、guest、Lua、DSL output 的路径均不得
相同；任一约束或 schema 支持检查失败时，编译器 fail fast，不写出部分 sidecar。
未传此选项时，默认生成内容、`TbeTypedType` ABI 与 production DataBind 依赖方向都不变。

### Consumer 链接

把 typed source 和 sidecar 编译进同一个 schema library。typed source 需要
`TurboParser::DataBind`，sidecar/decode 需要 `TurboUtils::CBind`；JSON 输入还要链接
安装态 consumer 使用公开 JSON facade `TurboParser::Parser`。在本仓库/build-tree 内部，
若直接使用 JSON DOM adapter，则链接实际 build target `json_parser`；它不是安装态的
imported target。两种 JSON 入口二选一，不需要把 `tbe_compiler` 部署到运行时。

```cmake
add_library(order_schema STATIC
  generated/order.c
  generated/order_cbind.c)
target_include_directories(order_schema PUBLIC generated)
target_link_libraries(order_schema
  PUBLIC TurboParser::DataBind TurboUtils::CBind)

# Installed consumer: public TurboParser JSON facade.
target_link_libraries(my_app PRIVATE order_schema TurboParser::Parser)
```

仓内/build-tree 的 direct adapter 路径改为：

```cmake
target_link_libraries(my_app PRIVATE order_schema json_parser)
```

### 生成的 API、所有权与限制

对每个 composite、group 与 message，header 会声明：

```c
const cmeta_data_desc *Type_cbind_data(void);
cbind_status Type_from_cserde(cbind_context *context,
                              cserde_reader *reader,
                              Type_t *object,
                              cbind_error *error);
```

`Type_cbind_data()` 返回 process-lifetime 的 immutable descriptor，调用方不释放它。
`Type_from_cserde()` 只转发给 `cbind_decode()`：不拥有 `context`、`reader` 或 `object`，
也不倒回 reader。`object` 在调用前必须已经由 `Type_init()` 初始化至 semantic zero；
成功后调用方通过 `Type_clear()` 释放 owning field，失败时 CBind 已将完整对象图恢复至
semantic zero，调用方仍必须调用一次 `Type_clear()` 以形成统一 cleanup 路径。

返回 `CBIND_OK` 表示成功；unknown/duplicate/missing field、token mismatch、range、
depth、container、buffer limit、source 与 target 错误会原样以相应 `cbind_status`
返回，详情写入 `cbind_error`。含 `string` 的记录必须使用
`CBIND_CONTEXT_WITH_BUFFERS_INIT`：其 scratch、`max_depth`、`max_container_items` 与
`max_buffer_bytes` 都是调用方提供的有界上下文；后者限制每个 decoded buffer。

`[name(...)]` 是 sidecar 接受的 CSerde map key；`[c(...)]` 只选择 generated C struct
member，因此只影响 descriptor offset，不改变外部 key。v1 精确支持 `int32`/`int32_t`、
`int64`/`int64_t`、`uint64`/`uint64_t`、`float`、`double`、owning `string`，以及由这些
字段递归组成的 composite/group/message。`[alias(...)]`、optional、`bool`、其他整数、
enum、uuid、bytes、fixed array、list/set/map、group collection 与 union 都在生成期
fail fast；不得据此省略字段或降级为其他 storage。

`int64` 的 CMeta storage 是 `long`。因此生成的 sidecar 对 `sizeof` 与 `_Alignof`
发出 C11 静态断言；在 Windows LLP64，`int64_t != long`，使用 `int64`/`int64_t` 的
sidecar 会明确编译失败。这是兼容性防线而不是可恢复错误：在目标 ABI 上编译生成的
`*_cbind.c`（以及 CI 的 MSVC Release build）验证该断言；需要 Windows 可编译的 v1
schema 时，避免该字段类型，不能用隐式转换替代。

### JSON DOM → CSerde → generated façade

以下 schema 只使用 v1 支持的 storage，并展示 `[name]` 与 `[c]` 分工：

```text
schema OrderSchema;

composite Header { int32 sequence; }
message Order {
  Header header;
  [name(eventId), c(event_id)] uint64 id;
  string note;
  double score;
}
```

以下示例使用公开 facade；DOM 被 reader 借用，故在 reader 销毁前一直保持 DOM 存活且
不修改它。scratch 的两个字节分别覆盖 `Header` 的一个 field 与 `Order` 的四个 field
所需的 presence workspace（数组比最小值大是允许的）。所有成功和失败路径均清理：

```c
#include "order.h"
#include <turbo_parser_json.h>

#include <cbind/cbind.h>
#include <stdint.h>

int decode_order(const char *json, size_t json_size) {
  static const size_t kMaxDepth = 2u;
  static const size_t kMaxBufferBytes = 64u;
  unsigned char scratch[2] = {0};
  cbind_context context = CBIND_CONTEXT_WITH_BUFFERS_INIT(
      scratch, sizeof(scratch), kMaxDepth, 0u, kMaxBufferBytes);
  cbind_error error = CBIND_ERROR_INIT;
  turbo_json_doc_t *dom = NULL;
  cserde_reader *reader = NULL;
  Order_t order;
  int result = 1;

  Order_init(&order); /* Required semantic-zero precondition. */
  if (turbo_parse_json((const uint8_t *)json, json_size, &dom) != 0)
    goto cleanup;
  reader = turbo_json_cserde_reader_create(dom, kMaxDepth);
  if (reader == NULL)
    goto cleanup;
  if (Order_from_cserde(&context, reader, &order, &error) != CBIND_OK)
    goto cleanup; /* order has already been restored to semantic zero. */

  /* Safely consume order.header.sequence, order.event_id, order.note, order.score. */
  result = 0;

cleanup:
  turbo_json_cserde_reader_destroy(reader);
  turbo_free_json(&dom);       /* Frees the DOM only after the reader. */
  Order_clear(&order);         /* Required after both success and failure. */
  return result;
}
```

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
