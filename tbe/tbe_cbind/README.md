# TbeCBind 运行时 schema plan

`TurboParser::TbeCBind` 是从 TBE schema 到 `TurboUtils::CBind` 的可选纯 C 运行时桥接层。
它把运行时 schema 语义与调用方提供的 native CMeta storage descriptor 组合起来，构建
plan 自有的 immutable semantic overlay，并把任意 `cserde_reader` 直接交给 CBind。

它不 include、调用、链接、适配或 fallback 到 `TurboParser::DataBind`。

## 路线选择

| 路线 | Schema 时机 | Native 契约 | 结果与所有者 |
| --- | --- | --- | --- |
| `TurboUtils::CBind` | 已完成反射 | 最终 semantic CMeta descriptor | 调用方拥有的 C struct |
| `TurboParser::TbeCBind` | 运行时 | TBE schema + caller-native CMeta descriptor | 通过 immutable overlay plan 填充调用方 C struct |
| `TurboParser::DataBind` | 运行时/构建期 | DataBind schema 与动态或 `TBE_TYPED_*` metadata | 独立的 dynamic-value/typed-conversion 路线 |
| `tbe_compiler --cbind-output` | 构建/CI | 生成的 owning struct 与 descriptor | Build-time sidecar，direct CBind |

DataBind 是独立的 TurboParser 模块。它拥有动态值、跨格式对象和自己的 typed conversion
模型；它不是 CBind 子层、adapter 或 fallback。Build-time sidecar 也不使用 TbeCBind：
它生成最终 immutable descriptor，并直接调用 CBind。

TBE schema 文本不能描述目标进程的 `sizeof`、`_Alignof`、`offsetof`、native member identity、
字符串所有权或 buffer callback。因此 schema-only binding 无法推断 C ABI。TbeCBind 要求
native CMeta graph；任一契约缺失或不兼容时，它会在读取业务输入前失败，绝不创建
dynamic-value fallback。

## 依赖与 CMake

```text
TurboParser::TbeCBind -> TurboParser::TbeSchema
                      -> TurboUtils::CBind
TurboParser::TbeCBind -X-> TurboParser::DataBind
generated CBind sidecar -> TurboUtils::Core
                        -> TurboUtils::CBind
generated CBind sidecar -X-> TurboParser::DataBind
```

配置阶段会对所选 TurboUtils package 做 C11 compile/link feature probe，而不是只比较版本号。
探针必须同时看到 8 组 fixed-width CMeta descriptor、完整 enum adapter ABI/facade、
`TurboUtils::Core` 导出的 canonical UUID type/shape/ops/data/validator，以及
`TurboUtils::CBind` 的 decode 入口；缺少任一项都会以可操作错误终止 configure。Fixed-width
descriptor 是 header-local metadata；UUID metadata 是 Core 的 process-wide external objects，
所以直接引用 UUID descriptor/validator 的 native provider 或 generated sidecar 必须链接
`TurboUtils::Core`。这不改变 TbeCBind 的公开依赖图：
`TurboParser::TbeCBind` 仍然只直接依赖 `TurboParser::TbeSchema` 与
`TurboUtils::CBind`，且没有 DataBind edge。

安装态 JSON consumer 的 CMake 如下：

```cmake
cmake_minimum_required(VERSION 3.20)
project(tbe_cbind_example LANGUAGES C)

find_package(TurboParser CONFIG REQUIRED)

add_executable(tbe_cbind_example main.c)
target_compile_features(tbe_cbind_example PRIVATE c_std_11)
target_link_libraries(tbe_cbind_example PRIVATE
  TurboParser::TbeCBind
  TurboParser::Parser) # JSON DOM/CSerde provider; replace for another format.
```

`TurboParser::TbeCBind` 是独立安装 target。示例链接 `TurboParser::Parser` 仅为取得 JSON
provider；可以换成 YAML、XML、CSV 或自定义 CSerde provider。

Schema 在构建期已知时，可让 `tbe_compiler` 独立生成 native record header 与 direct CBind
sidecar，不需要 typed source：

```powershell
tbe_compiler order.schema --lang c `
  --output generated/order.h `
  --cbind-output generated/order_cbind.c
```

```cmake
find_package(TurboParser CONFIG REQUIRED)

add_library(order_cbind STATIC generated/order_cbind.c)
target_include_directories(order_cbind PUBLIC
  generated
  "$<TARGET_PROPERTY:TurboParser::TbeSchema,INTERFACE_INCLUDE_DIRECTORIES>")
target_link_libraries(order_cbind
  PUBLIC TurboUtils::Core TurboUtils::CBind)
```

这里从 `TurboParser::TbeSchema` target 只读取生成头所需的公开 `tbe_wire.h` include
interface，并不链接该 library；sidecar source 的 decode 路径直接调用 CBind。Core 提供
generated owning-string adapter 使用的 `tstr_*` symbols，因此必须与 CBind 一起作为公开
依赖声明，并在 Windows 部署其 runtime DLL。独立模式不生成/编译 typed source，生成头
不 include `tbe_typed.h`，target 也不需要 DataBind include、compile definition、link
library 或运行时 DLL。若显式再传 `--source-output generated/order.c`，则保留既有 combined
生成行为；typed source 与 sidecar 是并列产物，前者才需要 DataBind。

## 完整 desired usage

以下 `main.c` 只使用 TbeCBind 与 JSON CSerde 路线。Native descriptor 使用真实 C member
名称；schema 单独选择外部 key。

```c
#include <tbe_cbind/tbe_cbind.h>

#include "turbo_cmeta_data.h"
#include "turbo_parser_json.h"
#include "turbo_str.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct Order {
  int quantity;
  tstr symbol;
} Order;

static const cmeta_data_buffer_shape order_string_shape = {
    CMETA_DATA_BUFFER_OWNED};
static const cmeta_data_desc order_string_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "example.order.string",
    .display_name = "owned string",
    .kind = CMETA_DATA_STRING,
    .storage_type = &turbo_tstr_cmeta_type,
    .shape = &order_string_shape,
    .buffer_ops = &turbo_tstr_cmeta_buffer_ops};

static const cmeta_type_identity order_identity =
    CMETA_TYPE_ID_ATOM_INIT("example.order");
static const cmeta_type_desc order_type = {
    .name = "Order",
    .size = sizeof(Order),
    .align = _Alignof(Order),
    .kind = CMETA_T_OBJECT,
    .identity = &order_identity};
static const cmeta_field_desc order_layout_fields[] = {
    {"quantity", "int", offsetof(Order, quantity), sizeof(int),
     _Alignof(int), &cmeta_type_int, NULL},
    {"symbol", "tstr", offsetof(Order, symbol), sizeof(tstr),
     _Alignof(tstr), &turbo_tstr_cmeta_type, NULL}};
static const cmeta_struct_desc order_layout = {
    "Order", sizeof(Order), _Alignof(Order), order_layout_fields, 2u};
static const cmeta_data_field_desc order_data_fields[] = {
    {"example.order.quantity", "quantity", offsetof(Order, quantity),
     &cmeta_data_int},
    {"example.order.symbol", "symbol", offsetof(Order, symbol),
     &order_string_data}};
static const cmeta_data_struct_shape order_shape = {
    &order_layout, order_data_fields, 2u};
static const cmeta_data_desc order_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "example.order.data",
    .display_name = "Order native storage",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &order_type,
    .shape = &order_shape};

int main(void) {
  static const char schema[] =
      "message Order { [c(quantity), name(amount)] int32 count; "
      "[c(symbol), name(ticker)] string label; }";
  static const char json[] = "{\"amount\":12,\"ticker\":\"SDK\"}";
  tbe_cbind_plan_options options;
  tbe_cbind_plan_error plan_error;
  tbe_cbind_plan *plan = NULL;
  turbo_json_doc_t *document = NULL;
  cserde_reader *reader = NULL;
  Order order = {0}; /* CBind semantic-zero 前置条件。 */
  unsigned char scratch[1] = {0};
  cbind_context context = CBIND_CONTEXT_WITH_BUFFERS_INIT(
      scratch, sizeof(scratch), 1u, 0u, 32u);
  cbind_error bind_error = CBIND_ERROR_INIT;
  int result = 1;

  tbe_cbind_plan_options_init(&options);
  tbe_cbind_plan_error_init(&plan_error);
  if (tbe_cbind_plan_create_from_text(
          schema, sizeof(schema) - 1u, "Order", sizeof("Order") - 1u,
          &order_data, &options, &plan, &plan_error) != TBE_CBIND_OK)
    goto cleanup;
  if (turbo_parse_json((const uint8_t *)json, sizeof(json) - 1u,
                       &document) != 0)
    goto cleanup;
  reader = turbo_json_cserde_reader_create(document, 1u);
  if (reader == NULL)
    goto cleanup;
  if (tbe_cbind_plan_decode(plan, &context, reader, &order, &bind_error) !=
      CBIND_OK)
    goto cleanup;
  if (order.quantity != 12 || order.symbol == NULL ||
      strcmp(order.symbol, "SDK") != 0)
    goto cleanup;

  result = 0;

cleanup:
  /* 在所有可能的 borrowed backing owner 存活时清理 native storage。 */
  if (cmeta_data_buffer_restore_zero(&order_string_data, &order.symbol) !=
          CMETA_OK &&
      result == 0)
    result = 2;
  turbo_json_cserde_reader_destroy(reader);
  turbo_free_json(&document);
  /* 如需诊断，必须在此之前消费 bind_error.shape/field。 */
  tbe_cbind_plan_destroy(plan);
  return result;
}
```

Destination 从 semantic zero 开始。成功后，调用方以 descriptor 的 clear/reset 协议清理
每个 owning native field。失败后，CBind 以事务语义把完整 graph 恢复到 semantic zero；
同一个 cleanup 路径仍然安全。

## 名称与双 overlay

每个字段有两个名称：

- `[name(...)]` 选择 CSerde map key；未指定时使用 schema field name。
- `[c(...)]` 选择真实 native C member；未指定时使用 schema field name。

TbeCBind 按 `[c]` 查找 native storage，再构建两个 plan-owned overlay：reflected layout
overlay 与 data-descriptor overlay。二者都以 `[name]` 暴露 semantic field name，同时保留
native member 的 offset、size、alignment、type 与 buffer adapter。CBind 要求两个 view
按 semantic name 一致；只修改其中一个 overlay 会破坏 lookup 或 rollback。生成的
`Type_cbind_data()` 等 semantic sidecar 已是 direct CBind descriptor；当 `[name]` 与
`[c]` 不同时，不得把它反过来作为 caller-native shape。

## v2 接受与拒绝矩阵

Scalar spelling 只接受下表列出的精确集合；没有 `f32`、`f64` 或大小写变体。每组第一项是
canonical spelling，后续项是等价输入 alias。

| Schema spelling | Generated/native C storage | Native CMeta 要求 |
| --- | --- | --- |
| `bool` | `bool` | `CMETA_DATA_BOOL` 与 canonical `cmeta_type_bool` |
| `int8` / `int8_t` / `i8` | `int8_t` | `CMETA_DATA_SINT`，8 bits，精确 size/alignment |
| `uint8` / `uint8_t` / `u8` / `byte` | `uint8_t` | `CMETA_DATA_UINT`，8 bits，精确 size/alignment |
| `int16` / `int16_t` / `i16` | `int16_t` | `CMETA_DATA_SINT`，16 bits，精确 size/alignment |
| `uint16` / `uint16_t` / `u16` | `uint16_t` | `CMETA_DATA_UINT`，16 bits，精确 size/alignment |
| `int32` / `int32_t` / `i32` | `int32_t` | `CMETA_DATA_SINT`，32 bits，精确 size/alignment |
| `uint32` / `uint32_t` / `u32` | `uint32_t` | `CMETA_DATA_UINT`，32 bits，精确 size/alignment |
| `int64` / `int64_t` / `i64` | `int64_t` | `CMETA_DATA_SINT`，64 bits，精确 size/alignment |
| `uint64` / `uint64_t` / `u64` | `uint64_t` | `CMETA_DATA_UINT`，64 bits，精确 size/alignment |
| `float` | `float` | `CMETA_DATA_FLOAT`，32 bits，canonical float storage |
| `double` | `double` | `CMETA_DATA_FLOAT`，64 bits，canonical double storage |
| `string` | `tstr`（generated）或 caller storage | 完整且 matching 的 OWNED/BORROWED public buffer adapter；CUSTOM 不支持 |
| `uuid` | `turbo_uuid_t` | canonical Core UUID identity、16-byte storage、OWNED shape 与 `turbo_uuid_cmeta_data_valid()` |

`enum` 是 named type，不是 scalar alias。普通 enum 默认以 `int32` 为 underlying，也可显式使用
上表 8 组 fixed-width integer 的任一 spelling。每个值必须落在 underlying width 和 CMeta
`int64_t` value domain 内；symbol 与 value 必须唯一。Native descriptor 必须是
`CMETA_DATA_ENUM`，具有完整 exact-version `cmeta_data_enum_ops`，storage kind/width/size/alignment
与 underlying 完全一致，并按声明顺序逐项匹配 enum name、item symbol、text 和 value。输入接受
声明 item 的 symbol、text，或精确等于已声明 value 的整数；未知 text/value 拒绝。

`flags` 与 ordinary enum 不同：位组合可能不是单个声明值，本阶段没有定义其 CBind assignment
和 rollback 语义，所以任何 flags declaration 都在 schema/generation 阶段返回 unsupported，
不会把组合值当普通 enum，也不会 fallback 到 DataBind。

`uuid` 的 CSerde 输入只接受恰好 36 bytes 的 canonical `8-4-4-4-12` 十六进制 string token；
大小写 hex 均接受，compact、braced、URN、错误连字符或错误长度均拒绝。Adapter 不依赖 NUL、
不分配、不保留 input slice；semantic zero 是 16 个零字节。

| 其他 schema 特性 | v2 结果 |
| --- | --- |
| composite/group/message、`[name]`、`[c]` | 递归支持；完整 native graph，semantic/native 名各自唯一 |
| alias attribute、optional/default | plan/generation 前拒绝；无 coercion/fallback |
| bytes、fixed array、list/set/map、group collection | 拒绝；无 partial plan |
| union/variant | 拒绝；无 dynamic-value fallback |

Fixed-width storage 不再借用 `int`/`long`/`size_t` ABI，因此 Windows LLP64 上的
`int64_t`/`uint64_t` 也使用精确 64-bit descriptor。Caller 的 reflected layout type 与 data
storage type 仍必须用同一 semantic identity、size 与 alignment 描述实际成员；schema 不会推断
任意 C ABI。

## 所有权、生命周期、线程、错误与 limits

- READY plan 拥有复制的 schema-derived name 与两个 overlay；不保留 schema text 或 parser
  AST。
- Plan 借用完整 native CMeta graph 与 callback；它们必须保持 immutable、reentrant，且
  生命周期覆盖到 `tbe_cbind_plan_destroy()`。
- Context/scratch、reader、destination 与 error 均由调用方拥有，只在一次 decode 中借用。
- Owning string adapter 复制输入。Borrowed adapter 只接受 `CSERDE_VIEW_STABLE`；其
  DOM/buffer/arena backing owner 必须存活到 native destination clear 完成。只销毁 reader
  wrapper 不会延长 backing storage 的生命周期。
- 当前 TbeSchema parser 是 control-plane facility，因此 factory 调用必须由外部串行化。
  每次 decode 使用独立的 context/scratch、reader、destination 与 error 时，READY plan
  可跨线程共享；所有 decode join 后才能 destroy。
- Plan 创建返回 `tbe_cbind_status` 与 versioned `tbe_cbind_plan_error`。Decode 原样返回
  CBind 的 `cbind_status/cbind_error`。诊断中的 `shape`/`field` 可能指向 plan 内部，plan
  destroy 后失效。
- `options` 必填且每个 limit 必须非零。`max_schema_bytes`、`max_types`、`max_fields`、
  `max_depth`、`max_name_bytes` 与 `max_plan_bytes` 限制 TbeCBind-owned 工作。
  `max_plan_bytes` 只覆盖 READY plan，不代表 TbeSchema parser 临时 AST 的峰值；decode
  limits 仍由 `cbind_context` 提供。

Bool、整数和浮点是 trivial values。UUID 是无堆所有权的 16-byte value；enum storage 的
semantic-zero、assign/read 与 restore 由 native enum ops 独占。Generated string 使用 owning
`tstr` adapter；caller-native string 按 descriptor 声明 copy 或 borrow。每次 decode 前整棵
destination 必须处于 descriptor 定义的 semantic zero；任一 narrow range、unknown enum、
malformed UUID、reader 或 callback 失败都会由 CBind 把此前已写入的 scalar/enum/UUID/owning
string 一并回滚到 semantic zero。同一 cleanup path 在成功或失败后都必须安全。

Plan compilation 只表示 schema AST 到 descriptor graph。TbeCBind 没有 MIR/BMIR、JIT、
运行时机器码生成、可执行内存或运行时 C/C++ 编译器。性能结论必须来自显式的
`benchmark_tbe_cbind` target；benchmark 分开报告 runtime plan creation 与 repeated decode，
并以 build-time sidecar 生成的 direct CBind 调用作为 decode baseline。两条 decode 路径
读取同一个 scalar/nested schema 文件、同一个 generated C struct、同一 token sequence，
使用相同 context limits，并在各自测量前执行相同次数的 warm-up、重置 reader/scratch，且
每个 destination 都从 semantic zero 开始并在测量后恢复 semantic zero。

该 target 以独立 `--output + --cbind-output` 模式只生成并编译 struct header 与 CBind
sidecar source；不会生成 typed source，生成头和 include interface 不含 DataBind，target
按顺序只声明 `TurboUtils::Core` 与 `TurboUtils::CBind`，也不定义 `WITH_DATABIND`。它只比较 generated
sidecar direct CBind 与 TbeCBind plan façade；两条路径最终调用同一个 `cbind_decode()`。
benchmark 不执行、测量或比较 DataBind，结果不能用于判断 CBind 与 DataBind 的性能是否
相当；同一台机器上的微小数值差异也不支持路径间的原因归纳。

2026-08-24 在 Windows 11 `10.0.26200`、AMD Ryzen 9 7940HX、MSVC `19.44.35217`、
`win-release-user` Release 上单次运行的可复验事实如下：

| Case | Samples | avg/op | min/sample | max/sample | ops/s |
| --- | ---: | ---: | ---: | ---: | ---: |
| runtime TBE schema plan creation | 100 | 21.889 us | 19.800 us | 46.100 us | 45,685 |
| generated sidecar direct CBind decode | 10,000 | 0.373 us | 0.300 us | 6.900 us | 2,681,612 |
| runtime TbeCBind plan decode | 10,000 | 0.362 us | 0.300 us | 2.900 us | 2,763,882 |

这是一次本机 microbenchmark，没有统计置信区间；表中两个 decode 数字的微小差异不作
性能归因，也不能外推到其他输入、机器或 DataBind。

`benchmark_cbind_vs_data_bind` 是单独的公平性比较 target，也是该目录中唯一有意链接
`TurboParser::DataBind` 的 TbeCBind benchmark。两条路径共享同一份 schema、同一段 52-byte
JSON 和同一个 generated `TbeCBindBenchEnvelope_t` destination，并分别报告：

- setup：CBind 的 schema plan creation；DataBind 的 codec creation 加 typed descriptor
  schema validation。
- end-to-end decode：CBind 的 JSON DOM parse、CSerde reader creation 和 plan decode；
  DataBind 的 JSON parse 和 typed struct materialization。

输出初始化、warm-up、字段正确性校验，以及持久 plan/codec 和输出数组等 benchmark-owned
资源的清理位于计时区外。每次调用产生的 JSON DOM、CSerde reader、DataBind temporary/value
及其 teardown 均属于对应的 end-to-end 计时。由于 DataBind 当前没有公开的 typed
CSerde-reader API，该 target 不把 CBind 的预生成 token kernel 与 DataBind 的完整 JSON 路径
混称为等价比较；若只需观察 CBind binding kernel，应继续运行 `benchmark_tbe_cbind`。

本次 v2 没有把 scalar/enum/UUID 强塞进现有 benchmark。事实原因是
`benchmark_tbe_cbind.schema` 和同一个 generated sidecar 同时被 CBind-only baseline 与
`benchmark_cbind_vs_data_bind` 的 typed DataBind comparison 消费；修改该共享 struct/schema 会
改变已记录的 CBind-vs-DataBind 输入、wire layout、JSON byte count 与 typed descriptor
methodology。另建一套只为 smoke 的 benchmark fixture 又会超出当前 bounded baseline 结构。
正确性与 bounded decode 已由 runtime/generated C/C++/JSON tests 覆盖；未来若新增独立且同方法
的 v2 benchmark，应作为单独可比基线评审，而不是改写现有数字的含义。

同一环境连续五次运行的 `avg/op` 中位数与范围如下；每次运行内部仍使用表中的 samples：

| Case | Samples/run | median avg/op | avg/op range | median ops/s |
| --- | ---: | ---: | ---: | ---: |
| CBind: TBE plan creation | 100 | 23.045 us | 22.559–23.700 us | 43,393 |
| DataBind: codec creation + typed schema validation | 100 | 35.663 us | 34.390–42.561 us | 28,040 |
| CBind: JSON DOM + CSerde + plan decode | 10,000 | 6.156 us | 1.853–6.626 us | 162,448 |
| DataBind: JSON parse + typed struct materialization | 10,000 | 7.078 us | 4.317–8.837 us | 141,285 |

这些数字只说明该机器、该标量/嵌套输入下两条完整路径处于相近量级；运行间抖动明显，
不足以证明普遍等价，也不能把差异归因到 binding、JSON parser 或 allocator 中某一层。
