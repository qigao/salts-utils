# TurboParser CBind、Parser 与 CFlow 组合架构设计

日期：2026-08-23
状态：Design / 已在对话中确认核心 ownership，待 written-spec review
基线：`main@c9d0903e1134211431e7bce7dd0e5e1001746d9c`

本设计取代此前把 CBind 作为 `TurboUtils::CBind` 的 ownership 方案。此前 D2 中关于 scalar/struct decode、context-first、scratch、preflight、rollback、numeric conversion 的技术约束继续作为本设计的 CBind kernel 契约；变化的是仓库归属、公开 target、Parser direct adapter 与 CFlow 组合边界。

## 1. 已锁定的架构决策

以下决策为本设计的固定前提：

1. CBind 属于 `qigao/turbo-parser`，不是 TurboUtils 产品。
2. CBind 是独立 public target：`TurboParser::CBind`，应用允许直接链接和调用。
3. CBind 是 top-level module：`cbind/`，不是 `parser/cbind/`、`tbe/cbind/` 或 `tbe/data_bind/cbind/`。
4. public include 使用 `<cbind/...>`；C ABI 使用自然 `cbind_*` 命名，不增加 `turbo_` 或 `turbo_parser_` 前缀。
5. CBind core 严格 format-neutral，只认识 CMeta semantic descriptor 与 CSerde canonical token contract。
6. `TurboParser::CBind` production dependency 仅为 `TurboUtils::CMeta + TurboUtils::CSerde`。
7. CBind core 不依赖 `TurboParser::Parser`、DataBind、TBE、TurboSTL、CFlow 或具体 JSON/YAML/XML/CSV parser。
8. `turbo_parser.h` 不 re-export CBind；DataBind public headers 也不 re-export CBind。
9. 首个 CBind D2 PR 只建立 kernel，不同时重构现有 DataBind/TbeTyped。
10. DataBind/TbeTyped migration 为后续独立阶段。
11. CFlow 不是 CBind core 的依赖。CFlow integration 属于可选 composition layer。
12. raw `cserde_token` 不作为普通业务 `Stream<T>` 元素暴露给 `filter/map`；CFlow 从完整 semantic value / bound native object 边界开始。

## 2. Ownership 与职责

整体职责固定为：

```text
TurboUtils
├── CMeta
│   └── C type identity / reflection / semantic data shape / container contracts
├── CSerde
│   └── canonical token ABI + reader/writer protocol
├── CFlow
│   └── generic lazy stream / graph / runtime
└── TurboSTL
    └── standard containers + CMeta/CFlow adapters

TurboParser
├── parser/*
│   └── native syntax parsers, SAX/event/DOM/query
├── cbind/
│   └── native C object <-> canonical semantic values
├── CBind parser adapters
│   └── parser events <-> CSerde canonical projection <-> CBind
├── CBindFlow
│   └── parser + CBind + CFlow composition
├── DataBind
│   └── runtime schema/dynamic tree/compatibility/query host
└── TBE
    └── specialized binary schema/wire/layout
```

因此 repo ownership 与 target dependency 必须分开理解：CBind 位于 TurboParser repo，但其 kernel 仍然保持比 Parser/DataBind 更低、更纯的依赖层级。

## 3. Public identity 与目录

目标目录：

```text
cbind/
  CMakeLists.txt
  include/cbind/
    cbind.h
    status.h
    context.h
    error.h
    decode.h
  src/
    decode.c
    scalar.c
    struct.c
    machine.c
    internal.h
  tests/
    CMakeLists.txt
    cbind_context_test.c
    cbind_scalar_decode_test.c
    cbind_struct_decode_test.c
    cbind_transaction_test.c
    cbind_header_cpp_test.cpp
```

后续 parser adapter 可扩展：

```text
cbind/
  include/cbind/
    json.h
    yaml.h
    xml.h
    csv.h
  adapters/
    json.c
    yaml.c
    xml.c
    csv.c
```

CMake identity：

```text
concrete target: turbo_cbind
alias/export:    TurboParser::CBind
```

应用消费：

```cmake
find_package(TurboParser CONFIG REQUIRED)
target_link_libraries(app PRIVATE TurboParser::CBind)
```

```c
#include <cbind/cbind.h>
```

不使用：

```text
TurboUtils::CBind
TurboParser::Parser::CBind
<cbind/turbo_cbind.h>
turbo_cbind_decode(...)
turbo_parser_cbind_decode(...)
```

## 4. Dependency graph

### 4.1 CBind kernel

```text
TurboParser::CBind
    -> TurboUtils::CMeta
    -> TurboUtils::CSerde
```

CBind kernel 禁止直接依赖：

```text
TurboParser::Parser
TurboParser::DataBind
TurboParser::TbeSchema
TurboUtils::STL
TurboUtils::Core
TurboUtils::CFlow
JSON/YAML/XML/CSV concrete parser targets
```

### 4.2 Parser direct adapters

格式 adapter 依赖方向必须是 adapter 依赖 CBind 与 concrete parser，而不是 CBind kernel 反向依赖 parser：

```text
CBindJsonAdapter -> CBind + JsonParser
CBindYamlAdapter -> CBind + CYaml
CBindXmlAdapter  -> CBind + XmlParser
CBindCsvAdapter  -> CBind + CsvParser
```

具体 adapter 是否作为单独 CMake target 或聚合到一个 convenience target，在实现对应 adapter 的独立设计阶段决定；D2 不引入它们。

### 4.3 CBindFlow

CFlow composition 的逻辑依赖：

```text
TurboParser::CBindFlow
    -> TurboParser::CBind
    -> relevant parser adapters
    -> TurboUtils::CFlow
```

`TurboParser::CBind` 不反向依赖 CBindFlow。

## 5. 为什么 CBind 在 TurboParser，而 CMeta/CSerde 留在 TurboUtils

CMeta 是 C language semantic/type contract；CSerde 是 canonical data-event ABI。二者都可被 parser 之外的 producer/consumer 使用，例如 network source、database row adapter、synthetic test source，因此继续属于 TurboUtils 基础能力。

CBind 则是把 canonical semantic value 映射到 native C object 的 binding product。TurboParser 已经拥有 DataBind、TbeTyped、格式 parser 与 TBE，继续把 CBind 放在 TurboUtils 会形成两个 native-binding ownership center：

```text
TurboUtils::CBind      generic native binding semantics #1
TurboParser::TbeTyped  native binding semantics #2
TurboParser::DataBind  native binding semantics #3
```

把 CBind 置于 TurboParser，可让后续 DataBind/TbeTyped 向同一 kernel 收敛，同时仍通过 target 边界保持 kernel 的最小依赖。

## 6. CBind kernel 的数据模型

CBind core 的输入不是 JSON/YAML/XML/CSV，而是 canonical CSerde token：

```text
NULL
BOOL
SINT
UINT
FLOAT
STRING
BYTES
ARRAY_BEGIN / ARRAY_END
MAP_BEGIN / MAP_END
```

CBind core 的 semantic truth 来自 `cmeta_data_desc`。

核心关系：

```text
cmeta_data_desc + cserde token sequence
                 |
                 v
               CBind
                 |
                 v
          native C storage
```

CBind 不根据 concrete parser node 类型猜语义，也不要求 DataBindValue DOM 作为中间表示。

## 7. 内部 execution primitive：token-fed binding machine

为了同时支持 pull `cserde_reader` 与 parser SAX push path，内部真正的 decode primitive 应设计为 token-fed state machine，而不是把 reader loop 与 binding semantics 永久耦合。

概念内部接口：

```c
/* internal, not D2 public ABI */
cbind_status cbind_machine_begin(...);
cbind_status cbind_machine_accept(..., const cserde_token *token);
cbind_status cbind_machine_finish(...);
```

公共 generic decode：

```c
cbind_status cbind_decode(
    const cbind_context *context,
    const cmeta_data_desc *shape,
    cserde_reader *reader,
    void *out,
    cbind_error *error);
```

其实现语义等价于：

```text
preflight
machine_begin
while machine needs input:
    cserde_reader_next
    machine_accept(token)
machine_finish
```

这样未来 JSON/YAML SAX adapter 可直接把 canonicalized event feed 给 machine，不需要构造 DOM 或 token queue。

D2 不公开 machine API。只有当跨模块 adapter 需要稳定 ABI 且内部直接复用无法满足时，才另行设计 public/integration contract。

## 8. D2 scope

首个 `TurboParser::CBind` PR 只实现 decode-only scalar + struct kernel。

支持：

```text
CMETA_DATA_BOOL
CMETA_DATA_SINT
CMETA_DATA_UINT
CMETA_DATA_FLOAT
CMETA_DATA_STRUCT
```

D2 对以下合法 semantic kind 返回 `CBIND_UNSUPPORTED`：

```text
CMETA_DATA_STRING
CMETA_DATA_BYTES
CMETA_DATA_ENUM
CMETA_DATA_VARIANT
CMETA_DATA_SEQUENCE
CMETA_DATA_SET
CMETA_DATA_MAP
CMETA_DATA_CUSTOM
```

D2 明确不实现：

```text
encode
string/bytes lifecycle
containers
optional/default/alias
custom adapters
decode_replace
unknown-field skip policy
parser direct adapters
CBindFlow
DataBind migration
TbeTyped migration
TBE wire migration
```

## 9. D2 public status

```c
typedef enum cbind_status {
    CBIND_OK = 0,
    CBIND_INVALID_ARGUMENT,
    CBIND_INVALID_CONTEXT,
    CBIND_INVALID_SHAPE,
    CBIND_DESTINATION_NOT_EMPTY,
    CBIND_TOKEN_MISMATCH,
    CBIND_VALUE_OUT_OF_RANGE,
    CBIND_UNKNOWN_FIELD,
    CBIND_DUPLICATE_FIELD,
    CBIND_MISSING_FIELD,
    CBIND_UNEXPECTED_END,
    CBIND_LIMIT_EXCEEDED,
    CBIND_UNSUPPORTED,
    CBIND_SOURCE_ERROR
} cbind_status;
```

语义：

- malformed argument/output record -> `CBIND_INVALID_ARGUMENT`；
- malformed context -> `CBIND_INVALID_CONTEXT`；
- malformed CMeta semantic/storage graph -> `CBIND_INVALID_SHAPE`；
- valid but unsupported semantic kind/storage -> `CBIND_UNSUPPORTED`；
- valid canonical token incompatible with expected semantic value -> `CBIND_TOKEN_MISMATCH`；
- numeric conversion not exact/safe -> `CBIND_VALUE_OUT_OF_RANGE`；
- reader normal `CSERDE_DONE` while value incomplete -> `CBIND_UNEXPECTED_END`；
- reader real failure -> `CBIND_SOURCE_ERROR` with exact `cserde_status` preserved；
- scratch/depth budget insufficient -> `CBIND_LIMIT_EXCEEDED`。

## 10. Context-first API

CBind public API 固定 context-first：

```c
cbind_decode(ctx, shape, reader, out, error);
```

未来对称 API 也保持：

```text
cbind_encode(ctx, shape, object, writer, error)
cbind_decode_replace(ctx, shape, reader, out, error)
```

`context` 是 reusable execution environment，不是 trailing options；`error` 是单次操作结果，不放入 context。

D2 context：

```c
enum { CBIND_CONTEXT_ABI_VERSION = 1u };

typedef struct cbind_context {
    size_t struct_size;
    uint32_t abi_version;
    void *scratch;
    size_t scratch_size;
    size_t max_depth;
} cbind_context;
```

使用 caller-side header initializer，避免 future library 写大于 caller object 的 append-safe ABI 问题。

## 11. Error record

D2 error：

```c
enum { CBIND_ERROR_ABI_VERSION = 1u };

typedef struct cbind_error {
    size_t struct_size;
    uint32_t abi_version;
    cbind_status status;
    cserde_status source_status;
    const cmeta_data_desc *shape;
    const cmeta_data_field_desc *field;
    size_t depth;
} cbind_error;
```

不得保存 transient CSerde key slice。

known field failure / duplicate / missing 可保存稳定的 `cmeta_data_field_desc *`；unknown field 保持 `field == NULL`。

## 12. Preflight 与 no-consumption guarantee

在第一次 `cserde_reader_next()` 之前必须完成：

```text
1. argument / error-record validation
2. context validation
3. complete D2 semantic/storage graph preflight
4. max-depth preflight
5. scratch-budget preflight
6. destination empty-state validation
```

上述任一失败：

```text
reader provider calls = 0
out mutation          = 0
```

D2 不允许在读到一半后才发现 schema 中另一个字段根本无法写入。

## 13. Scratch 与 depth

Scratch 只用于 binder bookkeeping，不用于 object staging。

每个 active struct frame 的 seen bitmap：

```text
(field_count + 7) / 8 bytes
```

所需 scratch 是最深 active struct path 上各 frame bitmap 之和；退出 nested frame 后 bump cursor rewind，因此 sibling 可复用。

```text
max_depth = 0 -> scalar root only
root struct   -> depth 1
nested struct -> depth 2, 3, ...
```

零字段 struct 不需要 bitmap bytes，但仍消耗一个 struct depth。

## 14. Destination empty-state 与 transaction

D2 empty state：

```text
BOOL        false
SINT/UINT   0
FLOAT       numeric zero (+0/-0 both empty)
STRUCT      every semantic field recursively empty
```

只检查 semantic graph，不检查 padding，也不要求 reflected-but-nonsemantic fields 为零。

D2 decode 是 construct-new-value contract：

```text
precondition: semantic destination is empty
success:      all semantic fields initialized
failure:      semantic destination reset to empty
```

CBind 只保证 destination rollback，不保证 reader rollback：

```text
failure:
    out    -> entry empty state
    reader -> remains after consumed prefix
```

不得伪造 rewind、自动 skip 或 retry。

reset 只递归写 semantic fields，不 `memset` 整个 struct；padding 与 nonsemantic fields 必须保持不变。

## 15. D2 scalar storage proof

D2 不根据 `bits` 猜任意用户 scalar storage。

仅支持当前 canonical built-in writable storage：

```text
BOOL   -> cmeta_type_bool
SINT   -> cmeta_type_int / cmeta_type_long
UINT   -> cmeta_type_size
FLOAT  -> cmeta_type_float / cmeta_type_double
```

并要求 semantic width 与真实 storage width 一致。

其他自定义 scalar storage 即使 semantic kind 合法，也返回 `CBIND_UNSUPPORTED`，直到 CMeta 增加显式 storage adapter/lifecycle contract。

不得为了 CBind 把所有 int8/uint8/int16/... 强行加入 CMeta default known/callable type universe。

## 16. Numeric conversion policy

BOOL 只接受 `CSERDE_BOOL`。

整数接受安全 numeric conversion：

```text
SINT -> SINT : target range
UINT -> SINT : <= signed max
SINT -> UINT : >= 0 && <= unsigned max
UINT -> UINT : target range
FLOAT -> INT : finite + integral + target range
```

float target：

```text
FLOAT -> FLOAT
  float64 direct
  float32: finite overflow rejected
           nonzero underflow-to-zero rejected
           normal IEEE rounding allowed
           NaN/Inf allowed

SINT/UINT -> FLOAT
  must be exactly representable
```

因此例如 `9007199254740993ULL` 不得静默绑定成 binary64 `9007199254740992`。

禁止 string->number、bool->number coercion。

## 17. Struct canonical grammar

Struct canonical value：

```text
MAP_BEGIN
  STRING(field-name) value
  STRING(field-name) value
MAP_END
```

D2 state machine：

```text
expect MAP_BEGIN
reserve/zero seen bitmap
loop:
    next token
    MAP_END -> verify all required fields seen
    STRING key -> exact field lookup
                  unknown   => CBIND_UNKNOWN_FIELD
                  duplicate => CBIND_DUPLICATE_FIELD
                  decode child recursively
                  mark seen only after child succeeds
    other -> CBIND_TOKEN_MISMATCH
```

field name 必须 exact/case-sensitive。CSerde STRING slice 不保证 NUL，因此字段查找使用 length + bytes compare，不为查找分配临时 C string。

D2 所有 semantic fields 都 required；没有 optional/default。

failure reader position：

```text
unknown field    -> key consumed, value not consumed
duplicate field  -> duplicate key consumed, duplicate value not consumed
non-string key   -> key token consumed, paired value not consumed
```

空 semantic struct 接受 `{}`。

## 18. Parser direct binding

CBind kernel 保持 format-neutral，但 TurboParser 可以提供 direct convenience path，使普通应用不必手工构造 `cserde_reader`。

目标用户体验：

```c
#include <cbind/json.h>

MyConfig config = {0};

cbind_status st = cbind_json_decode(
    &ctx,
    MyConfigData(),
    json,
    json_len,
    &config,
    &error);
```

同类入口可包括：

```text
cbind_json_decode
cbind_yaml_decode
cbind_xml_decode
cbind_csv_decode
```

这些 API 属于 parser-specific adapter/composition，不属于 CBind kernel 的 dependency surface。

### 18.1 JSON

JSON raw SAX 已能保留 exact numeric token，因此可：

```text
json_parse_sax_raw
    -> canonicalize event
    -> cbind_machine_accept
```

Object/array 可自然投影为 MAP/ARRAY。

### 18.2 YAML

YAML SAX 已提供 scalar kind，可投影为 canonical NULL/BOOL/SINT/UINT/FLOAT/STRING 与 MAP/ARRAY。

YAML alias/tag 等不能在没有明确 canonical policy 时偷偷展开；adapter 必须定义后才能进入对应实现阶段。

### 18.3 XML

XML SAX 的 element/attribute/text 不是天然 canonical MAP，因此 XML adapter 必须拥有显式 projection policy。该 policy 属于 TurboParser XML binding adapter，不进入 CBind/CMeta core。

### 18.4 CSV

CSV SAX 是 row/field 模型，不是天然 nested canonical object。CSV adapter 负责 header/column -> semantic field projection；D2 不设计 dotted/indexed CSV shape。

## 19. 为什么不暴露 Stream<cserde_token>

CSerde token 是结构协议，不是普通独立元素集合。

如果允许业务 pipeline：

```text
Stream<cserde_token>
    -> filter
    -> map
```

业务代码可能删除 `MAP_END`、field key 或改变 key/value pairing，直接破坏 canonical grammar。

因此规范固定：

```text
CSerde token stream = internal structural transport
CFlow Stream<T>      = complete semantic values / bound native values
```

对 token 的 projection/validation 必须由 parser adapter / CBind machine 控制，而不是普通 CFlow operators。

## 20. CBindFlow：Parser + CBind + CFlow composition

CBindFlow 的目标是把多值输入统一成 native typed stream：

```text
parser source
    -> complete value boundary
    -> CBind
    -> native object T
    -> CFlow Stream<T>
    -> filter/map/flatMap/distinct/sorted/limit/collect
```

概念 API：

```c
cflow_json(json, len, &stream)
    ->bind(&stream, UserData(), &ctx)
    ->filter(&stream, active)
    ->map(&stream, normalize)
    ->collect(&stream, UserVec_collector(&users), max_output);
```

也可由 format-specific constructor 直接产出 bound stream：

```c
cbind_json_array_stream(&ctx, UserData(), json, len, &stream);
```

最终公开 surface 在 CBindFlow 独立设计阶段确定；本设计只锁定 execution boundary，不把这些概念签名视为 D2 ABI。

### 20.1 适合成为 Stream<T> 的 source

```text
JSON root array items
JSONPath matches
YAML sequence items
YPath matches
CSV rows
XML/XPath matches
```

每一个 emitted stream item 必须是一个完整 semantic value，成功绑定成独立 native object 后才进入 CFlow operator chain。

### 20.2 Backpressure 与 lifetime

CBindFlow 必须保持 pull/lazy 或显式 bounded push execution，不允许 parser 无限积压 native objects。

bound object 的生命周期必须覆盖 CFlow 对该 item 的同步消费；需要 retained/async execution 时必须通过 CMeta lifecycle/collector 明确取得 ownership，不得借用 parser callback transient buffer。

对应细节留给 CBindFlow 独立 spec。

## 21. CFlow 的职责边界

CFlow 继续只负责 execution semantics：

```text
filter
map
flatMap
peek
limit/take
skip
sorted
distinct
collect
match/find/reduce
...
```

CFlow 不负责：

```text
JSON number parsing
YAML scalar inference
XML element->field policy
CSV header projection
CMeta struct field write
CBind numeric range validation
CSerde token grammar
```

因此：

```text
CSerde = canonical structural protocol
CBind  = semantic value <-> native C object
CFlow  = native object execution pipeline
```

## 22. DataBind 与 TbeTyped migration boundary

首个 CBind D2 PR 不修改：

```text
tbe/data_bind/data_bind.c
tbe/data_bind/data_bind.h
tbe/data_bind/tbe_typed.c
tbe/data_bind/tbe_typed.h
```

原因：当前 DataBind 同时包含 runtime schema、dynamic value tree、format binding、query、stream、TBE 与 typed binding。若在建立新 public CBind ABI 的同时改写该模块，会把新 ABI、跨 package dependency、typed metadata migration 与 400KB 级实现重构混在一个 PR。

后续独立 migration：

```text
Phase M1
  TbeTyped generic semantic metadata
      -> project/replace with CMeta descriptors

Phase M2
  DataBind typed/native path
      -> delegate generic binding to CBind

Phase M3
  retain TBE-only wire metadata
      -> wire offset / endian / presence / fixed block / group / var-data
```

最终目标是移除重复 generic semantic truth，而不是一次性删除 DataBind dynamic/runtime 功能。

## 23. TBE boundary

TBE specialized metadata 继续留在 TurboParser/TBE：

```text
wire kind
wire offset
endianness
presence bitmap
fixed block
variable data
group semantics
```

这些不是 generic CMeta semantic descriptor。

CMeta 描述 logical/native semantics；TBE 描述 concrete binary wire/layout。二者允许 bridge，但不得合并成同一个 descriptor。

## 24. Package/export

CBind 是 `TurboParserTargets` 的一等 exported target，但不加入 `turbo_parser.h` umbrella。

```text
#include <cbind/cbind.h>    -> explicit CBind consumer
#include <turbo_parser.h>   -> existing parser umbrella only
```

D2 不引入 `find_package(TurboParser COMPONENTS CBind)`；沿用当前：

```cmake
find_package(TurboParser CONFIG REQUIRED)
```

随后按 target 链接。

CBind target 的 transitive link interface 必须只传播其真实依赖。整个 TurboParser build 仍可因其他模块要求 `TurboUtils::Core/STL`，但这些不得因此进入 `TurboParser::CBind` 的 PUBLIC link interface。

## 25. Tests

### 25.1 D2 kernel tests

至少覆盖：

```text
context ABI / scratch validation
error record ABI validation
scalar success / token mismatch / range
signed/unsigned native-width boundaries
float->int finite/integral/range
int->float exactness: 2^24 / 2^24+1, 2^53 / 2^53+1
float64->float32 overflow / nonzero underflow / normal rounding / NaN / Inf
root + nested struct
field order independence
transient non-NUL key slices
unknown / duplicate / missing / non-string key
exact scratch threshold
zero-field struct with zero scratch
max_depth exact boundary
unsupported nested shape preflight without reader consumption
malformed storage/layout preflight without reader consumption
non-empty destination without reader consumption
partial-write failure rollback
padding/nonsemantic fields unchanged
reader position after failure
CSerde source error mapping
C++17 public header compatibility
```

Windows tests must从 `sizeof(long)` 推导 boundary，不假设 LP64。

### 25.2 Parser adapter tests（后续阶段）

每个 adapter 需要验证：

```text
native parser error -> adapter/CBind error boundary
exact number preservation
transient callback view does not escape
no mandatory DOM/token queue
canonical projection correctness
format-specific policy isolated from CBind core
```

### 25.3 CBindFlow tests（后续阶段）

需要验证：

```text
one complete native value per stream item
filter/map never observe structural tokens
short-circuit does not over-parse beyond allowed source semantics
bounded buffering/backpressure
borrowed/transient parser data does not escape callback
collector ownership
pipeline error is sticky and preserves originating parser/CBind status
```

## 26. CI 与验证

D2 implementation 完成后最低验证：

```text
fresh Linux configure/build/full CTest
fresh Windows configure/build/full CTest
selected CMeta/CSerde/CBind tests
C++17 public header consumer
installed-package consumer linking TurboParser::CBind
```

必须验证 installed target 的 interface，不只验证 build-tree alias。

Parser adapter/CBindFlow 后续各自增加 exact-head Linux + Windows CI。

## 27. Migration sequence

推荐顺序固定为：

```text
1. 本设计文档完成 review
2. CBind D2 implementation plan
3. TurboParser::CBind scalar + struct decode kernel
4. installed-package + Linux/Windows conformance
5. JSON direct adapter
6. YAML direct adapter
7. XML/CSV projection designs and adapters
8. CBindFlow design/implementation
9. TbeTyped generic semantic metadata migration
10. DataBind native path delegation to CBind
11. remove duplicated generic binding facts after equivalent coverage exists
```

步骤 5–10 都不能被塞回 D2 首个 PR。

## 28. Superseded TurboUtils documents

以下 TurboUtils design content 不再是 ownership truth：

```text
docs/superpowers/specs/2026-08-23-cbind-scalar-struct-decode-design.md
  old target: TurboUtils::CBind

docs/superpowers/specs/2026-08-23-serialization-data-binding-design.md
  old ownership: TurboUtils owns CBind
```

其 CBind algorithm/transaction/numeric reasoning 可作为历史设计依据，但 repository ownership、target、module layout、parser/CFlow integration 以本文件为准。

## 29. Final invariants

后续实现不得破坏以下不变量：

```text
TurboUtils does not depend on TurboParser.

CBind core does not depend on parser/*.
CBind core does not depend on CFlow.
CBind core does not depend on DataBind/TBE.

Parser adapters may depend on CBind.
CBindFlow may depend on parser adapters + CBind + CFlow.
DataBind may later depend on CBind.

CSerde token grammar is structural transport, not business Stream<T>.
CFlow elements begin at complete semantic/native value boundaries.

CMeta remains semantic/type truth.
CSerde remains canonical token truth.
CBind remains native binding truth.
Parser remains syntax truth.
TBE remains binary wire/layout truth.
CFlow remains execution truth.
```
