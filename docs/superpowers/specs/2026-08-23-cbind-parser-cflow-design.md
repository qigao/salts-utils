# TurboParser Parser / CBind / CFlow 集成设计

日期：2026-08-23
状态：Design / 已确认 ownership 与 integration boundary，待 written-spec review
基线：`main@c9d0903e1134211431e7bce7dd0e5e1001746d9c`

本文件只定义 TurboParser 如何消费 TurboUtils 的 CSerde/CBind/CFlow。它不拥有 CBind kernel，也不复制 CBind D2 contract。

CBind 的 normative ownership / D2 设计位于 `qigao/turbo-utils`：

```text
branch: design/cbind-scalar-struct-decode
spec:   docs/superpowers/specs/2026-08-23-cbind-scalar-struct-decode-design.md
amend:  docs/superpowers/specs/2026-08-23-cbind-parser-integration-amendment.md
```

## 1. 已锁定决策

```text
TurboUtils owns CMeta + CSerde + CBind + CFlow.
TurboParser owns concrete parser syntax/query, format projection, DataBind and TBE.
```

具体约束：

1. CBind 不迁入 TurboParser。
2. TurboParser 不导出 `TurboParser::CBind`。
3. CBind public target 保持 `TurboUtils::CBind`。
4. CBind core 保持 format-neutral，只依赖 `TurboUtils::CMeta + TurboUtils::CSerde`。
5. TurboParser format adapters 可以依赖 concrete parser + CSerde + CBind。
6. TurboUtils 不反向依赖 TurboParser。
7. DataBind/TbeTyped 可在后续阶段向 CMeta/CBind 收敛，但该消费关系不改变 CBind ownership。
8. raw `cserde_token` 不作为普通业务 `Stream<T>` 暴露给 CFlow `filter/map`。
9. CFlow 业务元素从完整 semantic/native value boundary 开始。
10. 首个 CBind D2 PR 不修改 TurboParser。

## 2. 整体 architecture

```text
TurboUtils
├── CMeta
│   └── semantic/type truth
├── CSerde
│   └── canonical token truth
├── CBind
│   └── canonical semantic value <-> native C storage
├── CFlow
│   └── generic execution pipeline
└── TurboSTL
    └── standard containers

TurboParser
├── parser/*
│   └── JSON/YAML/XML/CSV/... syntax + SAX/DOM/query
├── format binding adapters
│   └── parser event/value -> CSerde/CBind integration
├── DataBind
│   └── runtime schema / dynamic values / compatibility / query host
├── TBE
│   └── specialized binary schema/wire/layout
└── optional parser + CBind + CFlow composition
    └── complete native values as Stream<T>
```

关键原则：repo ownership 与 consumer relationship 分离。

TurboParser 是 CBind 的重要 consumer，但不是 CBind 的 owner。

## 3. Dependency direction

基础层：

```text
TurboUtils::CBind
    -> TurboUtils::CMeta
    -> TurboUtils::CSerde
```

TurboParser adapter：

```text
format adapter
    -> concrete parser/query
    -> TurboUtils::CSerde
    -> TurboUtils::CBind
```

未来 stream composition：

```text
parser/CBind/CFlow composition
    -> concrete parser/query
    -> format adapter
    -> TurboUtils::CBind
    -> TurboUtils::CFlow
```

禁止：

```text
TurboUtils::CBind -> TurboParser
TurboUtils::CBind -> TurboUtils::CFlow
TurboUtils::CFlow -> TurboParser
TurboParser private headers -> TurboUtils implementation
```

## 4. CBind generic path

高级/非-parser producer 可以直接使用 TurboUtils CBind：

```c
#include <cbind/cbind.h>

cbind_status st = cbind_decode(
    &ctx,
    shape,
    &reader,
    &object,
    &error);
```

这一路径不需要 TurboParser。

典型 producer 可以是：

```text
network source
database row adapter
IPC decoder
synthetic test source
custom protocol adapter
```

只要它们能够提供符合 CSerde contract 的 canonical reader，即可使用 CBind。

## 5. TurboParser direct format binding

TurboParser 可以提供普通应用更易用的 format-specific convenience：

```text
JSON input -> native object
YAML input -> native object
XML input  -> native object
CSV input  -> native object/record
```

但这些入口属于 TurboParser adapter/composition，不进入 `TurboUtils::CBind` core。

概念数据流：

```text
input bytes
    -> concrete parser
    -> format-specific semantic projection
    -> CSerde canonical representation
    -> TurboUtils::CBind
    -> native C object
```

本设计不锁定 direct API 的函数名、header 名或 CMake target 名。

例如 `cbind_json_decode(...)`、`turbo_bind_json(...)` 等只能作为讨论中的概念示例，不能被 implementation plan 当作已批准 ABI。

## 6. Pull reader 与 SAX push 的跨 repo 边界

当前 `TurboUtils::CBind` D2 public API 消费 pull `cserde_reader`。

TurboParser 的部分 parser 已经是 push SAX/event model。因此存在一个真实 integration problem：

```text
push parser -> ? -> pull cserde_reader
```

不能通过 TurboParser 调用 TurboUtils/CBind private `cbind_machine_*` 来解决，因为 private implementation 不是跨 repo ABI。

若后续 direct adapter 明确要求：

```text
no mandatory DOM
no unbounded token queue
bounded streaming execution
```

则应先独立设计一个 TurboUtils/CBind public incremental decoder contract，概念上可能是 begin/feed/finish，但具体名称、ABI、partial-value lifecycle、backpressure、error propagation 都必须单独 review。

D2 不提前增加这个 public surface。

因此：

```text
D2 = cbind_decode(cserde_reader *) only
future direct SAX zero-queue = separate integration design
```

## 7. JSON adapter

JSON 的 object/array event 与 canonical MAP/ARRAY 最接近。

JSON raw SAX 还可以保留 exact numeric token，因此未来 adapter 应避免先 round-trip 到 `double` 再绑定整数。

目标 projection：

```text
null         -> CSERDE_NULL
bool         -> CSERDE_BOOL
exact number -> canonical SINT/UINT/FLOAT according to JSON numeric parser policy
string       -> CSERDE_STRING
object       -> MAP_BEGIN / STRING key / value / MAP_END
array        -> ARRAY_BEGIN / value / ARRAY_END
```

JSONPath selection 属于 TurboParser query/source layer，不属于 CBind。

## 8. YAML adapter

YAML SAX 已拥有 scalar kind inference，可投影为 canonical：

```text
NULL / BOOL / SINT / UINT / FLOAT / STRING
SEQ -> ARRAY
MAP -> MAP
```

YAML alias、tag、merge key、multi-document 等语义必须由 YAML adapter policy 明确定义；CBind 不猜测 YAML 语义。

YPath selection 同样属于 TurboParser。

## 9. XML adapter

XML element/attribute/text 不是天然 canonical object model。

因此 XML adapter 必须显式定义：

```text
element -> field/object mapping
attribute -> field mapping
text -> scalar/value mapping
repeated child -> sequence policy
namespace/name policy
mixed content policy
```

这些全部属于 TurboParser XML binding policy，不进入 CMeta/CSerde/CBind。

XPath selection 属于 TurboParser query/source layer。

## 10. CSV adapter

CSV 是 header/row/column model，不是 nested canonical object grammar。

CSV adapter 负责：

```text
header -> semantic field mapping
row -> one complete record
cell -> scalar conversion/projection
missing/extra column policy
nested dotted/indexed column policy（若支持）
```

这些 policy 不进入 CBind core。

CSV row 是未来 CFlow native stream 的天然 item boundary。

## 11. 为什么不暴露 Stream<cserde_token>

CSerde token 是结构协议：

```text
MAP_BEGIN
STRING(key)
value
MAP_END
```

如果把它当普通 CFlow element：

```text
Stream<cserde_token>
    -> filter
    -> map
```

业务 operator 可以删除 `MAP_END`、删除 key、改变 key/value pairing，直接破坏 canonical grammar。

因此固定：

```text
CSerde token stream = structural transport
CFlow Stream<T>      = complete semantic/native values
```

CFlow 不承担 token grammar maintenance。

## 12. Parser + CBind + CFlow composition

推荐的 execution boundary：

```text
parser source
    -> one complete semantic value
    -> CBind
    -> one native object T
    -> CFlow Stream<T>
    -> filter/map/flatMap/distinct/sorted/limit/collect
```

适合作为 `Stream<T>` source 的单位：

```text
JSON root-array item
JSONPath match
YAML sequence item
YPath match
CSV row
XML/XPath match
```

每个 element 在业务 operator 可见之前必须形成完整 semantic/native value。

这保证 CFlow 可以继续遵守自己的 Java-style stream semantics，而不感知 parser token grammar。

## 13. Composition 的 target/API 名称暂不锁定

此前讨论中的 `TurboParser::CBindFlow` 只是 design label，不是已批准 target。

同样，下列概念 fluent 示例不属于当前 public ABI：

```c
cflow_json(...)
    ->bind(...)
    ->filter(...)
    ->collect(...);
```

最终是单独 target、format-specific targets、还是现有 Parser/DataBind 上的可选 integration surface，必须在 CFlow integration 独立 spec 中决定。

当前只锁定 dependency 与 semantic boundary。

## 14. Backpressure 与 lifetime

未来 parser/CBind/CFlow composition 必须满足 bounded execution：

```text
no unbounded native-object queue
no transient parser view escaping callback lifetime
short-circuit does not retain unrelated values
retained/async value requires explicit CMeta lifecycle/ownership operation
```

具体 pull/push/backpressure contract 在 stream integration spec 中定义。

## 15. DataBind migration

现有 DataBind 继续保留：

```text
runtime schema host
dynamic DataBindValue/DataBindObject
format APIs
query/path integration
compatibility surface
```

后续可逐步收敛 generic native binding：

```text
DataBind typed/native binding path
    -> TurboUtils::CBind
```

但 dynamic tree 不是 CBind 必须中间表示，也不因使用 CBind 而被强制删除。

## 16. TbeTyped migration

当前 TbeTyped 混合 generic native semantic metadata 与 TBE-specific wire metadata。

后续目标：

```text
generic semantic/native facts
    -> CMeta / CBind

TBE wire-only facts
    -> remain in TurboParser/TBE
```

TBE 保留：

```text
wire kind
offset
endianness
presence bitmap
fixed block
group
variable data
```

不得把这些 specialized wire facts 泛化进 CMeta。

## 17. Package boundary

TurboParser 不新增或导出 `TurboParser::CBind`。

应用若直接使用 generic CBind：

```cmake
find_package(TurboUtils CONFIG REQUIRED)
target_link_libraries(app PRIVATE TurboUtils::CBind)
```

TurboParser 自己在 format adapter target 中链接 `TurboUtils::CBind`。

`turbo_parser.h` 不需要 re-export `<cbind/cbind.h>`；消费 generic binder 的代码显式 include CBind header。

未来某个 direct-binding adapter 是否由 `turbo_parser.h` umbrella 暴露，留给该 adapter 的独立 public API design。

## 18. Implementation phases

推荐顺序：

```text
1. TurboUtils CBind D2 written-spec review
2. TurboUtils CBind D2 implementation plan
3. TurboUtils::CBind scalar + struct decode
4. TurboUtils installed-package + Linux/Windows verification
5. decide whether incremental public decoder is required
6. TurboParser JSON direct adapter
7. TurboParser YAML direct adapter
8. XML/CSV projection specs + adapters
9. Parser + CBind + CFlow stream composition
10. TbeTyped generic semantic metadata migration
11. DataBind native binding delegation
12. remove duplicated generic binding facts only after equivalent coverage exists
```

步骤 5–11 不进入 D2 PR。

## 19. Adapter tests

每个 format adapter 后续至少验证：

```text
parser syntax error propagation
canonical projection correctness
exact numeric preservation where format supports it
transient callback views do not escape
unknown/duplicate/missing semantics stay at correct layer
no hidden dependency from CBind back to Parser
format-specific policy does not leak into CMeta/CSerde/CBind
```

若实现 incremental direct path，还必须验证 bounded buffering/backpressure 与 partial failure cleanup。

## 20. CFlow integration tests

未来 stream composition 至少验证：

```text
one complete native value per stream item
filter/map never observe structural tokens
short-circuit respects source semantics
bounded retained state
transient parser memory does not escape
collector ownership is explicit
parser/CBind/CFlow errors preserve origin without duplicate logging
```

## 21. Non-goals

本设计不：

```text
implement CBind D2
change CBind public ABI
choose incremental decoder ABI
choose direct-binding function names
choose CBind/CFlow composition target name
rewrite DataBind
rewrite TbeTyped
move TBE wire metadata
expose raw cserde_token as business CFlow stream
```

## 22. Final invariants

```text
TurboUtils owns CMeta + CSerde + CBind + CFlow.
TurboUtils does not depend on TurboParser.

CBind depends only on CMeta + CSerde.
CBind does not depend on parser/* or CFlow.

TurboParser owns syntax/query and format projection.
TurboParser adapters may consume CBind.
TurboParser composition may consume CBind + CFlow.
DataBind may later consume CBind.

CSerde tokens are structural transport.
CFlow business elements start at complete semantic/native value boundaries.

CMeta  = semantic/type truth
CSerde = canonical token truth
CBind  = native binding truth
Parser = syntax/projection truth
TBE    = specialized binary wire/layout truth
CFlow  = execution truth
```
