# TbeCBind v2 Fixed Scalar and Enum Design

日期：2026-08-24

状态：Proposed

## 决策摘要

Issue #5 的第一阶段扩展 `TurboParser::TbeCBind` 与 `tbe_compiler --cbind-output`
的共同能力集，支持 `bool`、8/16/32/64 位有符号与无符号整数、`uuid` 和普通
`enum`。两条路径继续把执行交给 `TurboUtils::CBind`，不引入 DataBind、动态值树、
运行时代码生成或 JIT。

实现采用一个 TurboParser 内部、格式无关的 scalar capability matrix 作为 schema
拼写、语义类别、位宽和 generated-C metadata symbol 的唯一事实源。运行时 schema
model 与生成器都查询该矩阵；枚举在此基础上引用其底层整数能力。

固定宽度整数需要 TurboUtils CBind 先支持 descriptor 声明的 8/16/32/64 位整数
storage。该能力属于通用 CBind kernel，不在 TurboParser 复制。TurboParser PR 必须以
包含下列能力的 TurboUtils 版本为构建前置条件：

- 已合并的 enum storage adapter 与 enum decode（TurboUtils PR #80）；
- descriptor-driven fixed-width signed/unsigned decode；
- `turbo_uuid_t` 的 CMeta type、STRING buffer adapter 与 data descriptor。

## 现状证据

- `tbe/tbe_cbind/src/schema_model.c` 只识别 `int32/int64/uint64/float/double/string`，
  并在 schema 阶段整体拒绝 enum 声明。
- `tbe/tbe_cbind/src/native_shape.c` 把整数硬映射为 `int/long/size_t`，因此在 Windows
  上 `int64_t` 与 `long` 不同宽时会按设计 fail fast。
- `tbe/tbe_compiler/compiler_core.c` 维护另一份独立支持列表和相同的
  `int/long/size_t` ABI 假设；这是运行时与生成路径漂移的直接风险。
- TurboUtils CMeta 的 `cmeta_data_integer_shape.bits` 已可表达 8/16/32/64 位整数，
  但当前 CBind scalar kernel 只写 `int/long/size_t`。
- TurboUtils PR #80 已在 `master` 提供 enum ops、精确文本/数值匹配和事务回滚；
  TurboParser 不应复制该逻辑。

## 范围

本阶段支持：

- `bool`；
- `int8/int8_t/i8`、`uint8/uint8_t/u8/byte`；
- `int16/int16_t/i16`、`uint16/uint16_t/u16`；
- `int32/int32_t/i32`、`uint32/uint32_t/u32`；
- `int64/int64_t/i64`、`uint64/uint64_t/u64`；
- `float`、`double`、`string`（保持现有行为）；
- `uuid`，JSON 等文本 reader 以 canonical UUID string token 输入；
- 非 flags 的 `enum`，接受声明项的 symbol/text 或精确合法数值。

本阶段不支持：

- `flags` 组合值；CBind enum 只接受声明值，flags 的位组合语义需单独设计；
- optional/default/alias；
- bytes/list/set/map/group collection；
- union/variant；
- schema-only 推断任意 C ABI。

上述非范围构造继续在 schema/generation 阶段返回稳定的 unsupported 错误，不得降级到
DataBind 或按相近类型猜测。

## 候选方案

### 方案 A：仅放宽 TurboParser 校验

拒绝。当前 CBind 无法写窄整数，旧版本也无法写 enum。这样会产生可成功发布的 plan
或 sidecar descriptor，但在首次输入时返回 `CBIND_UNSUPPORTED`，破坏 preflight
承诺。

### 方案 B：在 TbeCBind 内复制 scalar/enum decoder

拒绝。runtime plan 可以绕过 CBind，但 generated sidecar 仍直接调用 CBind；若同时
复制两份执行逻辑，会形成三个事实源，并破坏格式无关 kernel 的依赖方向。

### 方案 C：把所有整数伪装成 `int/long/size_t`

拒绝。它依赖平台 ABI，不能表达 8/16 位 storage，在 Windows LLP64 上也不能表达
`int64_t` 为 `long`。静态断言只能拒绝平台，不能完成跨平台绑定。

### 方案 D：扩展通用 CBind，再由 TurboParser 共享 schema capability matrix

采用。CMeta descriptor 继续是 native storage 事实源；CBind 按 kind、bits、size 与
alignment 写入；TurboParser 只验证 TBE schema 与 native descriptor 是否同语义。
UUID 通过 STRING buffer adapter 把 canonical 文本转换为固定 16-byte value，不增加
格式专用 token kind。

## 架构与依赖

```text
TBE schema
   |
   +--> shared scalar capability matrix
   |       +--> runtime semantic model/native preflight
   |       `--> tbe_compiler sidecar annotations
   |
   +--> immutable CMeta overlay / generated CMeta descriptor
              |
              `--> TurboUtils::CBind --> cserde_reader --> C struct

DataBind -X-> every path above
```

能力矩阵放在 TbeSchema-owned internal C module中，因为 schema type normalization 是
TBE 语义，不属于 CBind。它导出给仓库内部目标使用的最小查询接口：输入 schema type
name，输出 canonical kind、signedness、bits、C storage spelling、CMeta type/data symbol
和特殊 UUID 标志。enum 不是矩阵中的伪 scalar；schema model 先查矩阵，再按声明表解析
named enum，避免把未知 record 误判为 enum。

该 internal header 不安装、不进入 `TurboParserConfig.cmake`；实现编译进现有
`TurboParser::TbeSchema`，从而不增加新的 target 或传递依赖。`TbeCBind` 与
`tbe_compiler` 只在 build tree 中通过 PRIVATE include path 调用它。函数符号属于
TbeSchema 内部实现，不形成受支持的用户 API。禁止在两处再次维护字符串列表。

## 固定宽度整数契约

TurboUtils CBind 的 integer preflight 使用以下不变量：

- kind 必须是 `CMETA_DATA_SINT` 或 `CMETA_DATA_UINT`；
- shape bits 只能是 8/16/32/64；
- `storage_type->size == bits / CHAR_BIT`，且目标平台 `CHAR_BIT == 8`；
- storage alignment 必须与 descriptor 自洽；
- 解码先用 token 的 64-bit canonical 值做范围检查，再通过对应的
  `intN_t/uintN_t` 临时值和 `memcpy` 写入，避免未对齐访问与 aliasing UB；
- float-to-integer 保持现有规则：有限、整数值、位宽范围内；失败返回
  `CBIND_VALUE_OUT_OF_RANGE`，destination 保持 semantic zero。

TurboUtils 提供 `turbo_cmeta_data.h` 中 header-local 的 fixed-width type/data descriptors，
地址不作为 identity；`cmeta_type_equal` 的 stable identity、kind、size、alignment 共同
决定兼容性。原有 `cmeta_data_int/long/size` 保持 ABI 和行为不变。

## UUID 契约

`uuid` 的 native storage 是 `turbo_uuid_t`，固定 16 bytes、无堆所有权。其 descriptor：

- `kind = CMETA_DATA_STRING`，因为 canonical CSerde 输入是文本 string token；
- storage type 为 `turbo_uuid_t` 的稳定 CMeta identity；
- buffer ownership 为 `CMETA_DATA_BUFFER_OWNED`，含义是 adapter 把输入复制/转换到目标
  value，不借用 token；
- semantic zero 是 16 个零字节；
- assign 只接受恰好 36 bytes 的 canonical `8-4-4-4-12` 十六进制文本，不依赖 NUL
  终止，不临时分配；
- 解析失败返回 `CMETA_INVALID_ARGUMENT`，CBind 映射为 target error 并恢复全零；
- restore_zero 清零全部 16 bytes，幂等且 no-fail。

runtime native shape 必须提供这个完整 adapter 或语义等价且 storage identity 一致的
descriptor。generated sidecar 直接引用 TurboUtils 提供的 UUID descriptor。普通
`string` 仍只接受 tstr/vstr 等 string storage，不得与 UUID 互换。

## Enum 契约

普通 enum 的 schema model 保存：name、canonical underlying scalar capability、item
symbol、解析后的 `int64_t` value。创建 plan 前必须验证：

- underlying type 是受支持的整数类型，且每个值在其 signed/unsigned 位宽范围内；
- enum/record/type 名无碰撞，item symbol 唯一，数值按 CMeta enum contract 唯一；
- native descriptor kind 为 `CMETA_DATA_ENUM`，enum ops 完整；
- native storage size/alignment 与 underlying capability 一致；
- schema item count、symbol/text/value 与 native `cmeta_enum_desc` 一一对应；
- flags declaration明确返回 `TBE_CBIND_UNSUPPORTED`。

plan overlay 复用已验证的 native enum data descriptor，因此 enum assignment、zero-state
和 rollback 仍由 TurboUtils CMeta/CBind adapter 独占。generated sidecar 为 schema enum
生成 immutable `cmeta_enum_desc`、fixed-width enum storage ops 与
`cmeta_data_desc`，C 与 C++ 消费者使用同一生成结果。

## 状态、所有权与失败

- schema AST 只在 plan factory/generator 调用期间存在；能力矩阵是只读静态数据。
- runtime semantic model 拥有 enum 名与 item 副本，随 model 销毁；plan 只保存完成
  preflight 后的 overlay 和对调用方 native descriptor 的借用，生命周期规则保持 v1。
- generated metadata 是 translation-unit static immutable storage。
- plan 发布仍是一次性事务：任一 scalar/enum/UUID shape 不匹配时 `*out == NULL`。
- decode 失败由 CBind 回滚已经写入的 scalar、enum、UUID 与 owning string；UUID 不分配，
  但仍通过统一 restore-zero 路径处理。
- 不增加全局缓存、锁或线程可变状态；ready plan 继续可并发只读共享。

## 兼容性与迁移

- 现有 v1 schema 和 public `tbe_cbind_*` 函数签名不变。
- 以前明确返回 unsupported 的类型开始成功，属于向后兼容的能力扩展。
- 生成 sidecar 不再为 `int64_t` 要求 `sizeof(long) == 8`，也不再为 `uint64_t` 要求
  `sizeof(size_t) == 8`；descriptor 使用精确 fixed-width storage。
- 构建需要包含 fixed-width/enum/UUID 能力的新 TurboUtils。CMake configure 必须做
  compile-time API/feature 检查并 fail fast，不能在旧依赖上静默缩减支持矩阵。
- 回滚方式是撤销 TurboParser capability commit 并恢复旧依赖 floor；不涉及数据迁移或
  wire format 变化。

## 性能影响

plan 创建阶段新增的 matrix lookup 为固定小表 O(1) 上界；enum item 校验为 O(n) 哈希或
排序校验，受现有 `max_fields/max_plan_bytes` 约束。decode 热路径只增加按 integer bits
的一次 switch；无分配、无字符串拷贝。UUID 解析固定扫描 36 bytes、O(1) space。

本阶段不宣称性能提升。现有 CBind/DataBind benchmark 保持可比；新增类型只需 smoke
benchmark 或回归阈值，优化必须由 profile 证明。

## 验证范围

- TurboUtils：8/16/32/64 signed/unsigned 成功与边界、wrong size/bits、float conversion、
  rollback、UUID 大小写/格式/长度/zero restore、C/C++ installed headers。
- runtime TbeCBind：所有 type alias、bool、UUID、enum 文本/数值、native mismatch、flags
  rejection、嵌套 record、JSON reader、失败回滚。
- generated sidecar：快照、C/C++ compile/run、Windows LLP64 上 int64/uint64、UUID 与 enum
  end-to-end、old unsupported constructs 仍 fail fast。
- dependency：生产 target 无 DataBind link/include；installed consumer 与 dependency
  closure tests 保持通过。
- 全量：Release build、focused tests、75+ CTest、install consumer、`git diff --check`，
  并确认无 `vendor/` 与 `.codegraph/` 提交。
