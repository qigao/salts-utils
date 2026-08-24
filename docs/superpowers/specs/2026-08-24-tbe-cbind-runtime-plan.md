# TBE CBind Runtime Plan Design

日期：2026-08-24

状态：Proposed

## 决策摘要

新增可选目标 `TurboParser::TbeCBind`，将运行时加载的 TBE schema 与调用方提供的
CMeta native shape 编译为 immutable `tbe_cbind_plan`。plan 直接把任意
`cserde_reader` 交给现有 `TurboUtils::CBind`，填充调用方的 C struct。

该模块必须独立于 DataBind：

```text
TurboParser::TbeCBind -> TurboParser::TbeSchema
                      -> TurboUtils::CBind

TurboParser::TbeCBind -X-> TurboParser::DataBind
TurboUtils::CBind     -X-> TurboParser
```

`-X->` 同时表示禁止 link、include、调用、类型复用与隐式 fallback。公开接口不得
出现 `DataBind`、`DataBindValue` 或 `TbeTypedType`。DataBind 保持现状，既不是
plan 的实现细节，也不是缺少 native shape 时的后备路径。

这里的“编译”是 schema AST 到 CMeta descriptor graph 的描述符编译，不生成机器码，
不使用 MIR/BMIR，不申请可执行内存，也不在运行时调用 C/C++ 编译器。

## 背景与约束

仓库已经存在两条完整但用途不同的路径：

- DataBind 把 schema 与输入格式投影为动态值或 `TbeTypedType` 描述的对象；它拥有
  自己的 AST、动态值、校验与转换生命周期。
- `tbe_compiler --cbind-output` 在构建期生成 static CMeta/CBind sidecar；这是已知
  schema 的低开销路径，不需要运行时解析 schema。

现在需要第三条、显式 opt-in 的路径：应用在运行时取得 schema，但仍要直接填充已有
C struct，并且整个调用链不经过 DataBind。

TBE schema 只能描述字段语义，不能从文本推导目标进程中的 `sizeof`、`_Alignof`、
`offsetof`、字符串所有权回调或容器操作。因而“只有 schema 就填充任意 C struct”在
C ABI 下不可实现。调用方必须提供 CMeta native shape；schema 决定外部字段名和类型
契约，native shape 决定实际 storage/layout。二者在 plan 创建时一次性校验。

这里的 native shape 是 storage contract：其 `cmeta_struct_desc.fields[].name` 必须是
真实 C member name，offset/size/alignment 必须来自目标 ABI。它不同于已经带有输入
semantic key 的 CBind sidecar descriptor。现有 `Type_cbind_data()` 应直接交给 CBind；
当 schema 使用 `[name]`/`[c]` 重命名时，不把该 semantic descriptor 再作为 runtime
plan 的 native input。

## 目标

- 运行时 schema 可直接驱动 CSerde -> C struct，不调用任何 DataBind API。
- JSON、YAML、XML、CSV 或自定义格式只需提供 `cserde_reader`，plan 不依赖格式 DOM。
- schema 的 `[name(...)]` 决定输入 key，`[c(...)]` 决定 native C member。
- 创建成功的 plan 是只读的，可跨线程共享执行。
- schema、native layout 或资源上限不匹配时，在读取业务输入之前 fail fast。
- 保持 `TurboUtils::CBind` 的格式中立与单向依赖，不在 TurboParser 中复制 decode
  kernel。
- 与构建期 sidecar 共存；不会替换或改变已有生成代码、DataBind ABI 或默认行为。

## 非目标

- 不从 schema 动态制造一种可由静态 C 表达式访问的新 struct 类型。
- 不新增动态万能值树；这会重新实现 DataBind 的职责。
- v1 不实现 alias、optional/default、union、enum 或通用 container 映射。
- 不缓存全局 plan，不加载插件，不热更新正在使用的 plan。
- 不把 `TbeTypedType` 转换为 CMeta，也不让 CBind 理解 TBE wire metadata。
- 不承诺通过 JIT 获得性能收益；性能判断必须来自 benchmark/profile。

## 候选方案

### 方案 A：复用 DataBind，再转交 CBind

拒绝。它违反独立使用要求，还会产生 schema AST、动态值和错误语义的双重生命周期。
即使包装层不暴露 DataBind 类型，运行时与链接依赖仍然存在。

### 方案 B：仅凭 schema 动态分配目标对象

拒绝。schema 不包含 native ABI layout 与 owning buffer 操作。若返回动态 map/value，
本质上是在 `TbeCBind` 内重建 DataBind，而不是绑定到调用方的 struct。

### 方案 C：TBE schema + 独立 native layout descriptor

可行但不采用。另造 descriptor 会重复 CMeta 的 size/alignment/offset/type/buffer ops，
形成两个事实源和新的适配成本。

### 方案 D：TBE schema + CMeta native shape -> immutable plan

采用。TbeSchema 继续负责语法，CMeta 继续负责 native storage，CBind 继续负责事务式
decode。新模块只拥有跨边界校验与 plan 生命周期。

### 方案 E：MIR/BMIR JIT

暂不采用。当前没有证据表明 descriptor preflight 或分派占端到端耗时 20% 以上；JIT
还会引入 executable-memory、安全策略、平台后端与 code-cache 生命周期。若 profiling
证明值得优化，应在 TurboUtils CBind 另行设计通用 compiled execution plan，而不是
把格式无关 kernel 下沉到 TBE 模块。

## 模块与依赖

新增目录与 target：

```text
tbe/tbe_cbind/
  CMakeLists.txt
  include/tbe_cbind/tbe_cbind.h
  src/...
  test/...

TurboParser::TbeCBind (STATIC)
  PRIVATE TurboParser::TbeSchema
  PUBLIC  TurboUtils::CBind
```

TbeSchema 的 `Node` 是面向 Mustache 的 mutable AST，不成为新公开 ABI。factory 内部
创建 root、调用 `parse_schema`、提取 plan 所需的只读语义，随后释放整棵 Node tree。
这避免调用者依赖 AST 的内部 key，也避免第三份公开 schema model。

`TurboParser::TbeCBind` 不命名为 `TurboParser::CBind`，因为 CBind kernel 的所有权仍在
TurboUtils。安装导出只增加 `TurboParser::TbeCBind`，不改变现有 target 的传递依赖。

## 公开接口草案

```c
#include <tbe_cbind/tbe_cbind.h>

typedef struct tbe_cbind_plan tbe_cbind_plan;

enum {
    TBE_CBIND_OPTIONS_ABI_VERSION = 1u,
    TBE_CBIND_ERROR_ABI_VERSION = 1u
};

typedef struct tbe_cbind_plan_options {
    size_t struct_size;
    uint32_t abi_version;
    size_t max_schema_bytes;
    size_t max_types;
    size_t max_fields;
    size_t max_depth;
    size_t max_name_bytes;
    size_t max_plan_bytes;
} tbe_cbind_plan_options;

typedef enum tbe_cbind_status {
    TBE_CBIND_OK = 0,
    TBE_CBIND_INVALID_ARGUMENT,
    TBE_CBIND_INVALID_OPTIONS,
    TBE_CBIND_SCHEMA_ERROR,
    TBE_CBIND_TYPE_NOT_FOUND,
    TBE_CBIND_NATIVE_SHAPE_ERROR,
    TBE_CBIND_TYPE_MISMATCH,
    TBE_CBIND_LIMIT_EXCEEDED,
    TBE_CBIND_UNSUPPORTED,
    TBE_CBIND_OUT_OF_MEMORY
} tbe_cbind_status;

typedef enum tbe_cbind_error_phase {
    TBE_CBIND_PHASE_NONE = 0,
    TBE_CBIND_PHASE_PARSE,
    TBE_CBIND_PHASE_SCHEMA,
    TBE_CBIND_PHASE_NATIVE_SHAPE,
    TBE_CBIND_PHASE_PLAN
} tbe_cbind_error_phase;

typedef struct tbe_cbind_plan_error {
    size_t struct_size;
    uint32_t abi_version;
    tbe_cbind_status status;
    tbe_cbind_error_phase phase;
    int line;
    int column;
    cmeta_status target_status;
    size_t field_index;
    char path[256];
    char message[256];
} tbe_cbind_plan_error;

void tbe_cbind_plan_options_init(tbe_cbind_plan_options *options);
void tbe_cbind_plan_error_init(tbe_cbind_plan_error *error);

tbe_cbind_status tbe_cbind_plan_create_from_text(
    const char *schema_text,
    size_t schema_size,
    const char *type_name,
    size_t type_name_size,
    const cmeta_data_desc *native_shape,
    const tbe_cbind_plan_options *options,
    tbe_cbind_plan **out,
    tbe_cbind_plan_error *error);

void tbe_cbind_plan_destroy(tbe_cbind_plan *plan);

const cmeta_data_desc *tbe_cbind_plan_shape(const tbe_cbind_plan *plan);

cbind_status tbe_cbind_plan_decode(
    const tbe_cbind_plan *plan,
    const cbind_context *context,
    cserde_reader *reader,
    void *out,
    cbind_error *error);
```

接口保持在 6 个函数。没有 `create_from_file`：文件读取与权限错误属于调用者边界，
factory 只接受带显式长度的内存。`options` 必填且所有 limit 必须非零，避免隐藏的
无界默认值。`options_init` 提供可修改的 bounded 推荐值：1 MiB schema、1024 types、
65536 fields、64 depth、255-byte name 与 16 MiB plan；实现前以实际 descriptor size
复算最坏内存，并用 named constants 固化。推荐值不会绕过 TbeSchema 已有的 10 MiB
parser hard cap。

`schema_text` 与 `type_name` 都是只在调用期间借用的 byte slice，不要求调用者额外
提供结尾 NUL。factory 在检查 `size + 1` 溢出与 byte limit 后创建 bounded NUL-terminated
副本，以满足现有 `parse_schema` 契约；slice 内含 NUL 时直接拒绝，防止 length 与 parser
可见内容分歧。`native_shape`、`options` 与 `out` 必填；`error` 可为 `NULL`，非空时必须
先由 `tbe_cbind_plan_error_init` 初始化。

`tbe_cbind_plan_decode` 是薄 façade，唯一执行动作是以 plan-owned shape 调用
`cbind_decode`。decode 继续使用现有 `cbind_context` 表达 scratch、depth、container
item 与 buffer byte 上限，并原样返回 `cbind_status/cbind_error`。

## 字段绑定与 plan 编译

对选中的 composite/group/message 递归执行：

1. 解析 schema，按 `type_name` 精确选择根类型；不做大小写或模糊匹配。
2. 校验完整 native CMeta storage graph。根必须是 `CMETA_DATA_STRUCT`；layout field
   name 是 C member identity，layout、storage type、field offset、size、alignment 与
   buffer ops 必须自洽。semantic-only layout name 不得按 declaration order 猜测。
3. 对每个 schema 字段计算两个名字：
   - semantic key：`[name(...)]`，缺省为 schema 字段名；
   - native member：`[c(...)]`，缺省为 schema 字段名。
   每个字段的 `[name]` 与 `[c]` 都至多出现一次。semantic key 必须匹配
   `[A-Za-z_][A-Za-z0-9_-]*`，native member 必须是有效 C identifier。每个 record
   内的 effective semantic key 和 effective native member 都必须唯一；mapped/mapped、
   mapped/canonical 两类碰撞同样拒绝，不采用“第一个 annotation 生效”的规则。
4. 在 `cmeta_data_struct_shape.layout` 中按 native member 精确查找 C field，再以 offset
   关联 native data field。不存在、重复 offset、类型不匹配或越界立即失败。
5. 同时构造 plan-owned reflected layout overlay 与 data overlay：对应的
   `cmeta_field_desc.name` 和 `cmeta_data_field_desc.name` 都使用 semantic key；
   offset/size/alignment/type/storage descriptor 来自 native member。CBind 要求 data
   field 能按同名在 reflected layout 中找到，所以不能把 semantic data fields 直接
   挂到原始 native layout。嵌套 struct 递归构造两层 overlay。
6. 检查 schema 与 native 字段一一对应。v1 不允许 schema 少字段、native 多字段或
   重复映射，防止 destination 的“未绑定尾部”产生不清晰的 zero/rollback 语义。
7. 完成全部 checked arithmetic 与资源计数后一次性发布 `*out`。失败时 `*out` 保持
   `NULL`，不留下部分 plan。

plan 不解析 C/C++ 源码，因此不支持通过 C 注释发现字段。绑定注解位于 TBE schema
的 `[name]` / `[c]` 属性；C 端布局通过 CMeta 宏、生成 sidecar 或手写的完整 CMeta
descriptor 提供。re2c/Lemon 只负责 TBE schema 语法，不参与 C ABI 反射。

## v1 支持矩阵

首版与已接受的 build-time sidecar 保持保守交集：

- `int32`：native storage 必须是目标 ABI 上的 canonical CMeta `int` 且恰为 32 bit。
- `int64`：native storage 必须是 canonical CMeta `long` 且恰为 64 bit。
- `uint64`：native storage 必须是 canonical CMeta `size_t` 且恰为 64 bit。
- `float`、`double`。
- owning/borrowed `string`，前提是 native descriptor 提供合法 buffer adapter。borrowed
  storage 只接受 `CSERDE_VIEW_STABLE`，并额外受 backing-owner 生命周期约束。
- 由上述字段递归组成的 composite/group/message。
- `[name(...)]` 与 `[c(...)]`。

v1 创建 plan 时拒绝：alias、optional/default、bool、小整数、`uint32`、enum、uuid、
bytes、fixed array、list/set/map、group collection 与 union。拒绝发生在读取业务数据
之前，不生成部分可用 plan，也不 fallback 到 DataBind。后续只有在 CBind/CMeta 已有
明确 native storage/ownership 契约且测试覆盖后，才逐项扩展矩阵。

## 状态、所有权与线程协议

核心状态只有 `tbe_cbind_plan`：

```text
create: EMPTY -> BUILDING -> READY
                     \-> FAILED（释放全部临时状态，out == NULL）
destroy: READY -> DESTROYED
```

- plan 拥有 schema-derived name、reflected layout/data field arrays、nested shape 与
  索引；不保留 Node AST 或 schema text pointer。
- plan 借用 `native_shape` 及其可达 layout/storage/buffer ops；它们必须 immutable，
  且生命周期覆盖 plan。static/generated CMeta descriptor 是推荐来源。
- reader、context、destination 与 error 均由调用者拥有，仅在一次 decode 调用中借用。
- reader 对象与 slice backing owner 是两个生命周期。owning adapter 成功后复制数据，
  source 可释放；borrowed adapter 成功后，产生 stable slice 的 DOM/buffer/arena owner
  必须一直存活到 destination 的 native clear/reset 完成。仅销毁 reader wrapper 不得
  被解释为 backing storage 仍有效；提前释放 owner 会产生悬空 view。
- `cbind_error.shape/field` 可能指向 plan-owned overlay；调用者必须在 destroy plan 前
  消费这些诊断指针，不得把它们作为独立长期对象保存。
- destination 必须处于 CBind 定义的 semantic zero；成功后由 native descriptor 对应
  的 clear/reset 协议释放，失败时 CBind 恢复完整 graph 到 semantic zero。
- factory 继承当前 TbeSchema parser 的控制面约束：`create_from_text` 不允许并发调用，
  调用者必须串行化。READY plan 可跨线程共享，只要每次 decode 使用互不共享的
  context/scratch、reader、destination 与 error。
- 借用的 native descriptor 与 buffer callbacks 也必须 immutable/reentrant；provider
  内部若有共享可变状态，由调用方在 descriptor 边界外串行化。plan 不替 provider
  增加锁或复制 callback 状态。
- `destroy` 是控制面操作，要求所有 decode 已结束；v1 不提供内部 refcount 或锁。

没有全局 registry/cache，避免 plan identity、eviction、锁与 native descriptor 生命周期
成为第二事实源。应用若缓存 plan，必须以 schema fingerprint + type name + native shape
版本共同作为 key，并在自身控制面负责替换与 quiescence。

## 容量、复杂度与失败语义

- 容量分两层。`max_schema_bytes` 在复制前限制输入，并受 TbeSchema 现有 10 MiB hard
  cap 约束；现有 parser 的 Node/Lemon 临时分配只由 schema byte cap 间接约束，尚不
  接受 allocator budget。`max_types/max_fields/max_depth/max_name_bytes` 在 AST 提取时
  执行，`max_plan_bytes` 只统计 TbeCBind-owned READY plan，不代表 create 全过程峰值。
  若需要 parser 临时内存的绝对预算，必须先给 TbeSchema 设计 bounded allocator API。
- TbeCBind 自己的所有加法和乘法均在分配前做 overflow 检查；超限不发布部分 plan。
- 创建时间为 `O(schema nodes + bound fields)`；实现应建立有界索引，避免按字段重复
  全树扫描导致 `O(n^2)`。plan 空间为 `O(bound fields + copied names)`。
- decode 的语义与现有 CBind 相同。v1 wrapper 不承诺减少 CBind 每次 decode 的 graph
  preflight；是否增加 TurboUtils-level compiled plan 必须另有 profiling 与 benchmark。
- schema parse/semantic 错误被复制到 `tbe_cbind_plan_error`，保留 line/column，而不把
  TbeSchema 的 `tbe_error_t` 暴露为 public ABI；native mismatch
  记录 phase、field index、稳定 path 与 CMeta status。decode 错误不转换，避免丢失
  source/field/depth/target attribution。
- 不记录 schema 内容、业务值或 buffer 数据；错误信息只包含类型/字段路径和阶段。

## Desired usage

```c
static const char order_schema[] =
    "message Order { [c(order_id), name(orderId)] uint64 id; "
    "string symbol; }";

tbe_cbind_plan *plan = NULL;
tbe_cbind_plan_error plan_error;
tbe_cbind_plan_options options;
tbe_cbind_plan_error_init(&plan_error);
tbe_cbind_plan_options_init(&options);

/* Order_native_data() 是以真实 C member name 描述 layout 的 CMeta storage shape。 */
tbe_cbind_status status = tbe_cbind_plan_create_from_text(
    order_schema, sizeof(order_schema) - 1u,
    "Order", sizeof("Order") - 1u,
    Order_native_data(), &options, &plan, &plan_error);
if (status != TBE_CBIND_OK) {
    /* 消费 plan_error；没有 DataBind 对象需要释放。 */
    return -1;
}

unsigned char scratch[4096];
cbind_context context = CBIND_CONTEXT_WITH_BUFFERS_INIT(
    scratch, sizeof(scratch), 8u, 0u, 4096u);
cbind_error bind_error = CBIND_ERROR_INIT;

const cbind_status bind_status = tbe_cbind_plan_decode(
    plan, &context, &json_reader, &order, &bind_error);

tbe_cbind_plan_destroy(plan);
```

输入不局限于 JSON；替换 `json_reader` 为任何遵守 CSerde 契约的 reader 即可。若 schema
在构建时已知，直接使用现有 generated `Type_from_cserde` 更简单，且不承担运行时
schema parse/plan allocation 成本。

## 兼容性、迁移与回滚

- 新 target、header 和 API 全部 opt-in；现有 DataBind、TbeSchema、tbe_compiler 输出与
  TurboParser façade 行为不变。
- 新消费者从 `TurboParser::DataBind` 迁移时不能直接复用 `TbeTypedType`；必须先提供
  独立 CMeta native shape，并显式处理 semantic-zero/clear。
- build-time sidecar 用户无需迁移，并应继续直接调用 generated `Type_from_cserde`；
  只有需要运行时 schema 且另有 native storage shape 时才创建 plan。
- 回滚只需停止链接 `TurboParser::TbeCBind` 并删除 plan 调用。没有数据格式迁移，也不
  修改 schema on-wire 表示。
- 若新模块撤回，TurboUtils CBind 与 DataBind 均不受影响。

## 验证与验收标准

- CMake target graph 断言 `TbeCBind` 的 direct/transitive dependencies 不含 DataBind；
  production source/header 用 `rg.exe` 验证不 include/call DataBind symbols。
- 独立 install consumer 只链接 `TurboParser::TbeCBind`，完成 schema + JSON CSerde ->
  native struct decode；不 include DataBind header。
- 单测覆盖 `[name]`、`[c]`、nested struct、owned string、invalid schema、unknown type、
  native layout/type mismatch、semantic-only layout 拒绝、全部 limits、checked
  overflow 与 OOM cleanup。
- annotation 负例覆盖 multiple/empty/unportable `[name]`、multiple/invalid `[c]`、
  mapped/mapped 与 mapped/canonical semantic/native collision；正例同时使用
  `[name]` 与 `[c]`。
- 事务测试覆盖 destination non-zero、unknown/duplicate/missing/range/source error、reader
  已消费语义与完整 rollback。
- 生命周期测试证明 schema text/Node 可在 create 后释放，native shape 必须存活，reader
  销毁不影响已复制的 owned string；borrowed string 覆盖 transient token 拒绝、stable
  token 成功、backing owner 在 clear 前保持存活，以及 clear 后才释放 owner。
- 并发测试只共享 READY plan，并给每线程独立的执行状态；TSan/ASan 配置可用时运行。
- benchmark 分开记录 schema plan creation 与 repeated decode，并以 generated sidecar
  direct CBind 为基线；没有测量证据时不声称 JIT 级性能。
- 完整构建/CTest、安装导出与消费者测试通过，diff 不包含 `vendor/` 或 `.codegraph/`。
