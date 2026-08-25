# `tbe_compiler --cbind-output`

`tbe_compiler` 是安装在 `${TURBOPARSER_ROOT}/bin` 的 build/CI 工具，不是运行时 link target。
`--cbind-output` 生成 owning C record header 与 direct `TurboUtils::CBind` sidecar；它不调用
TbeCBind、DataBind、JIT 或运行时 C/C++ compiler。

## 生成与使用

```powershell
tbe_compiler orders.schema --lang c `
  --output generated/orders.h `
  --cbind-output generated/orders_cbind.c
```

```cmake
find_package(TurboParser CONFIG REQUIRED)

add_library(orders_cbind STATIC
  generated/orders.h
  generated/orders_cbind.c)
target_include_directories(orders_cbind PUBLIC
  generated
  "$<TARGET_PROPERTY:TurboParser::TbeSchema,INTERFACE_INCLUDE_DIRECTORIES>")
target_link_libraries(orders_cbind PUBLIC
  TurboUtils::Core
  TurboUtils::CBind)
```

每个 generated record 提供 `Type_cbind_data()` 与
`Type_from_cserde(context, reader, object, error)`；ordinary enum 提供
`Enum_cbind_data()`。Sidecar metadata 是 translation-unit static immutable storage。含 UUID
时，MSVC 无法把 dllimport data address 放进静态初始化器，因此 generated schema 使用一个
`turbo_once` publication boundary，把所有 root/nested UUID slots 一次绑定到 canonical Core
objects；accessor 与 cold `Type_from_cserde()` 都经过同一边界。

`TurboUtils::Core` 是必需的公开链接项：它既提供 generated owning `tstr` symbols，也导出
canonical UUID type/shape/ops/data objects。Fixed-width integer descriptors 本身是 header-local，
但这不消除 generated sidecar 的 Core 依赖。`TurboUtils::CBind` 提供 decode kernel。该 target
保持 DataBind-free；只有显式再请求 `--source-output` 生成 typed DataBind source 时，调用方才为
那个并列产物单独链接 `TurboParser::DataBind`。

## v2 精确类型矩阵

下列 spelling 是 `--cbind-output` 接受的完整 scalar 集合；每行第一个是 canonical spelling，
没有额外 `f32`/`f64` 或大小写 alias。

| Schema spelling | Generated C storage | Generated descriptor |
| --- | --- | --- |
| `bool` | `bool` | canonical bool CMeta data |
| `int8` / `int8_t` / `i8` | `int8_t` | `turbo_int8_cmeta_data` |
| `uint8` / `uint8_t` / `u8` / `byte` | `uint8_t` | `turbo_uint8_cmeta_data` |
| `int16` / `int16_t` / `i16` | `int16_t` | `turbo_int16_cmeta_data` |
| `uint16` / `uint16_t` / `u16` | `uint16_t` | `turbo_uint16_cmeta_data` |
| `int32` / `int32_t` / `i32` | `int32_t` | `turbo_int32_cmeta_data` |
| `uint32` / `uint32_t` / `u32` | `uint32_t` | `turbo_uint32_cmeta_data` |
| `int64` / `int64_t` / `i64` | `int64_t` | `turbo_int64_cmeta_data` |
| `uint64` / `uint64_t` / `u64` | `uint64_t` | `turbo_uint64_cmeta_data` |
| `float` | `float` | canonical 32-bit float CMeta data |
| `double` | `double` | canonical 64-bit double CMeta data |
| `string` | `tstr` | schema-owned bounded OWNED adapter |
| `uuid` | `turbo_uuid_t` | canonical `turbo_uuid_cmeta_type/data` from Core |

Generated ordinary enum 默认 underlying 为 `int32`，也接受上表 8 组 fixed-width integer 的
任一 spelling。每个 value 必须同时落入 underlying width 与 `int64_t` domain，name/value 必须
唯一。Generator 产生 exact-version `cmeta_data_enum_ops`、immutable item/meta/shape/data，
输入由 CBind 接受 item 的 generated C symbol、schema text 或精确声明数值。Enum item 的派生
C symbol 会在 render 前与 public enum API 做完整 namespace collision 检查。

Flags 不等同 ordinary enum。位组合语义不在 v2 范围内，任何 flags declaration（即使没有被
record 引用）都会在写 header/source 前 fail fast。Optional/default/alias、bytes、collection、
group collection 与 union/variant 同样不生成 partial sidecar，也不 fallback 到 DataBind。

## UUID、所有权与 rollback

UUID reader 输入必须是恰好 36 bytes 的 canonical `8-4-4-4-12` hex string；大小写 hex 均可，
不接受 compact、braced、URN 或错误连字符。Canonical adapter 不依赖 NUL、不分配、不借用
token，semantic zero 是 16 个零字节。`turbo_uuid_cmeta_data_valid()` 是 native/runtime
admission authority；不能用相同 size 的普通 string adapter 代替。

Generated bool/integer/float/UUID/enum 是 value storage；generated string 是 owning `tstr`。
Destination 必须从 semantic zero 开始。成功后调用方按 descriptor 的 restore/clear 协议释放
owning fields；失败时 CBind 事务会把已写 scalar、enum、UUID 和 owning string 全部恢复为
semantic zero。Accessor 返回 borrowed process-lifetime descriptor，不得修改或释放。

## Dependency floor 与 benchmark 边界

TurboParser configure 会实际 compile/link 8 个 fixed-width descriptors、enum adapter facade、
canonical UUID Core exports/validator 与 CBind decode，而不是只检查 TurboUtils version string。
旧/缺失 package 会在 configure 阶段给出重建、安装及同-prefix target 检查建议。

现有 `benchmark_tbe_cbind.schema`/sidecar 同时驱动 CBind-only baseline 和
`benchmark_cbind_vs_data_bind` 的相同-input comparison。为加入 v2 smoke 而修改共享 schema 会
改变 struct/wire/JSON/typed-validation 方法；另建一套 benchmark fixture也不属于当前 bounded
结构，所以本阶段没有强行新增 scalar/enum/UUID benchmark。相关行为由 compiler、generated
C/C++、standalone、concurrent 与 JSON integration tests 验证。
