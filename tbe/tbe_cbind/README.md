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

## v1 支持与拒绝矩阵

| Schema 特性 | v1 结果 | Native storage 要求 |
| --- | --- | --- |
| `int32` | 支持 | canonical 32-bit CMeta `int` |
| `int64` | ABI 匹配时支持 | canonical 64-bit CMeta `long` |
| `uint64` | ABI 匹配时支持 | canonical 64-bit CMeta `size_t` |
| `float`、`double` | 支持 | canonical CMeta float descriptor 与宽度匹配 |
| `string` | 支持 | 有效的 OWNED 或 BORROWED public buffer adapter |
| composite/group/message | 递归支持 | 完整且匹配的 native struct graph |
| `[name]`、`[c]` | 支持 | effective semantic/native name 各自唯一 |
| alias、optional/default | decode 前拒绝 | 无 fallback |
| bool、小整数、`uint32` | decode 前拒绝 | 无 coercion |
| enum、uuid、bytes、fixed array | decode 前拒绝 | 无 partial plan |
| list/set/map、group collection、union | decode 前拒绝 | 无 dynamic-value fallback |

在 LLP64 系统（尤其 64-bit Windows）上，C `long` 不是 64 bit，`int64` schema 因而无法
满足 v1 canonical storage 规则。这是 ABI mismatch，不是 conversion request。

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
