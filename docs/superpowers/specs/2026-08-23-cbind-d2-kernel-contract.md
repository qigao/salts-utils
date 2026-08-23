# TurboParser CBind D2 Kernel Contract

日期：2026-08-23
状态：Normative companion / 待 written-spec review
基线：`main@c9d0903e1134211431e7bce7dd0e5e1001746d9c`
上位架构：`docs/superpowers/specs/2026-08-23-cbind-parser-cflow-design.md`

本文件是首个 `TurboParser::CBind` implementation plan 的 normative D2 contract。它把此前 TurboUtils D2 设计中仍然有效的 algorithm / ABI / transaction / numeric 约束迁到新的 repo ownership 下，避免实现阶段继续跨 repo 读取旧 ownership 文档。

## 1. Scope

D2 只实现：

```text
format-neutral
decode-only
canonical POD scalar + semantic struct
context-first
caller-supplied scratch
forward-only cserde_reader
transactional destination rollback
```

支持 semantic kind：

```text
CMETA_DATA_BOOL
CMETA_DATA_SINT
CMETA_DATA_UINT
CMETA_DATA_FLOAT
CMETA_DATA_STRUCT
```

以下合法 kind 在 D2 返回 `CBIND_UNSUPPORTED`：

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

D2 不实现 parser-specific direct adapter、CBindFlow、DataBind/TbeTyped migration、encode、replace、container、string/bytes lifecycle、optional/default/alias、unknown-field skip 或 custom adapter。

## 2. Module identity

```text
repo:       qigao/turbo-parser
module:     top-level cbind/
public:     <cbind/...>
C ABI:      cbind_*
CMake:      TurboParser::CBind
production: C11
```

production dependency：

```text
TurboParser::CBind
    -> TurboUtils::CMeta
    -> TurboUtils::CSerde
```

禁止 CBind kernel 依赖 Parser、DataBind、TBE、TurboSTL、Core 或 CFlow。

## 3. Public status

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

`CBIND_TOKEN_MISMATCH` 只表示 reader 已成功给出一个合法 canonical token，但 token kind/grammar 与 expected semantic value 不兼容。

实际 CSerde failure 不转换成 token mismatch；统一为 `CBIND_SOURCE_ERROR` 并保留 exact source status。

## 4. Context-first public API

```c
cbind_status cbind_decode(
    const cbind_context *context,
    const cmeta_data_desc *shape,
    cserde_reader *reader,
    void *out,
    cbind_error *error);
```

未来同族 API 继续 context-first：

```text
cbind_encode(ctx, shape, object, writer, error)
cbind_decode_replace(ctx, shape, reader, out, error)
```

D2 只实现 `cbind_decode`。

## 5. Context ABI

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

v1 required prefix 到 `max_depth` field-end，不以未来 `sizeof(cbind_context)` 作为兼容下限。

初始化必须由 caller-side header macro/static-inline 完成，例如：

```c
#define CBIND_CONTEXT_INIT(scratch_ptr, scratch_bytes, depth_limit) \
    { sizeof(cbind_context), CBIND_CONTEXT_ABI_VERSION, \
      (scratch_ptr), (scratch_bytes), (depth_limit) }
```

禁止由未来 library-side exported initializer 按 library 自己的更大 `sizeof(cbind_context)` 写入旧 caller object。

validation：

```text
context != NULL
struct_size >= field_end(max_depth)
abi_version == CBIND_CONTEXT_ABI_VERSION
scratch_size > 0 => scratch != NULL
scratch_size == 0 => scratch may be NULL or non-NULL
```

没有 process-global allocator/policy/default mutable context。

同一 context/scratch 在并发 decode 时由调用方外部串行化；一次 decode 结束后 CBind 不保留 scratch state。

## 6. Error ABI

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

caller-side initializer：

```c
#define CBIND_ERROR_INIT \
    { sizeof(cbind_error), CBIND_ERROR_ABI_VERSION, CBIND_OK, \
      CSERDE_OK, NULL, NULL, 0u }
```

如果 `error != NULL`，必须在 reader consumption 前验证其 v1 prefix/ABI。invalid error object -> `CBIND_INVALID_ARGUMENT`，不得尝试越界 diagnostic write。

成功时 normalized：

```text
status        = CBIND_OK
source_status = CSERDE_OK
shape         = NULL
field         = NULL
depth         = 0
```

binder-originated failure：

```text
source_status = CSERDE_OK
```

reader incomplete EOF：

```text
status        = CBIND_UNEXPECTED_END
source_status = CSERDE_DONE
```

reader actual failure：

```text
status        = CBIND_SOURCE_ERROR
source_status = exact cserde_status
```

`shape` 指 failure-site semantic descriptor，不一定是 root。

`field`：

```text
known child decode failure -> canonical field descriptor
duplicate                 -> duplicated canonical field
missing                   -> first missing field in semantic descriptor order
unknown/non-string key    -> NULL
struct-level source error -> NULL unless already inside resolved known field
```

不得把 transient STRING key slice 存进 error。

## 7. Validation order / zero-consumption contract

第一次 `cserde_reader_next()` 之前必须完成：

```text
1. required argument validation
2. optional error record validation
3. context validation
4. recursive D2 semantic/storage graph preflight
5. complete max_depth preflight
6. complete scratch-budget preflight
7. destination empty-state validation
```

上述任一失败：

```text
reader provider calls = 0
reader state mutation  = 0
out semantic mutation  = 0
```

D2 不允许先消费 input，再发现 semantic graph 的另一个必需字段根本 unsupported/malformed。

## 8. General semantic preflight

每个 visited descriptor：

```text
desc != NULL
cmeta_data_desc_valid(desc) == true
```

malformed -> `CBIND_INVALID_SHAPE`。

valid-but-D2-unsupported kind/storage -> `CBIND_UNSUPPORTED`。

CMeta 的 `cmeta_data_desc_valid()` 是 shallow semantic validity；CBind 必须做 writable-storage 与完整 nested graph preflight。

## 9. Canonical scalar storage proof

D2 不以 semantic bits 猜 arbitrary C representation。

允许写入的 canonical builtin storage：

```text
CMETA_DATA_BOOL  -> cmeta_type_bool
CMETA_DATA_SINT  -> cmeta_type_int / cmeta_type_long
CMETA_DATA_UINT  -> cmeta_type_size
CMETA_DATA_FLOAT -> cmeta_type_float / cmeta_type_double
```

SINT/UINT/FLOAT 要求：

```text
shape.bits == storage_type->size * CHAR_BIT
```

canonical builtin descriptor 自身若宣称冲突 width -> `CBIND_INVALID_SHAPE`。

valid semantic scalar 使用其他 native storage -> `CBIND_UNSUPPORTED`，不得 bytewise guess。

不得通过扩展 CMeta default known/callable type universe 来“解决” int8/uint8 等 storage；这会扩张 callable signature family，与 D2 无关。

## 10. Struct storage proof

对 `CMETA_DATA_STRUCT`：

```text
shape != NULL
shape->layout != NULL
storage_type != NULL
storage_type->size  == shape->layout->size
storage_type->align == shape->layout->align
```

每个 semantic field 必须满足：

```text
name/stable_id/value valid
field name unique within semantic shape
resolve exactly one reflected field by name
semantic offset == reflected offset
child semantic storage size == reflected field size
child semantic storage alignment == reflected field alignment
offset + child_size overflow-safe and <= parent storage size
no two semantic entries alias the same reflected field
child graph recursively passes D2 preflight
```

reflected fields 没有进入 semantic shape 是允许的；它们不属于 CBind initialization/rollback surface。

## 11. Descriptor cycle safety

malicious/invalid semantic descriptor graph 不得导致 preflight 无限递归。

preflight 使用 C call stack 上的 DFS validation frame，frame 保存：

```text
current semantic descriptor
parent validation frame
```

进入 child struct 前扫描 active ancestor chain；同一 semantic descriptor address 再次出现在 active chain -> `CBIND_INVALID_SHAPE`。

cycle detection 不使用 heap，也不消耗 caller scratch。

同时在 recursive entry 前检查 `max_depth`，因此 depth 与 cycle 都 fail-fast 在 reader consumption 前。

## 12. Depth semantics

```text
scalar root   -> depth 0
root struct   -> depth 1
nested struct -> depth 2, 3, ...
```

因此：

```text
max_depth = 0 -> scalar root legal, struct root rejected
max_depth = 1 -> root struct legal, nested struct rejected
```

进入超过 limit 的 struct -> `CBIND_LIMIT_EXCEEDED`，且因完整 preflight 发生在 input 前，provider calls 仍为 0。

## 13. Scratch model

scratch 只用于 seen-field bookkeeping，不用于 destination/object staging。

一个 active struct frame 的 bitmap：

```text
(field_count + 7) / 8 bytes
```

byte bitmap 不要求额外 alignment。

nested struct 使用 bump cursor；退出 nested frame 后 cursor rewind 到 entry mark，siblings 复用同一空间。

exact required scratch：

```text
max over every struct nesting path (
  sum((field_count + 7) / 8 for every simultaneously-active struct)
)
```

zero-field struct 需要 0 bitmap bytes，但仍计入 depth。

preflight 计算完整 supported graph 的 exact required scratch。若超出：

```text
CBIND_LIMIT_EXCEEDED
reader provider calls = 0
```

runtime frame allocation 仍需 defensive bounds check。

## 14. Destination empty state

D2 entry precondition：所有 semantic fields 处于定义的 empty state。

```text
BOOL        false
SINT        0
UINT        0
FLOAT       numeric zero; +0 and -0 both empty
STRUCT      every semantic child recursively empty
```

只检查 semantic graph：

```text
ignore padding
ignore reflected-but-nonsemantic fields
```

目的不是证明整个 raw object bytewise zero，而是证明 CBind 将要拥有/rollback 的 semantic region 当前没有已有 value。

non-empty -> `CBIND_DESTINATION_NOT_EMPTY`，reader 0 consumption。

## 15. Transaction semantics

D2 是 construct-new-value，不是 replace：

```text
entry:   semantic destination empty
success: all semantic fields initialized
failure: semantic destination returns to empty
```

failure 只 rollback destination，不 rollback streaming source：

```text
out    -> empty semantic graph
reader -> remains after already-consumed prefix
```

禁止 fake rewind、auto-skip、silent retry。

rollback/reset：

```text
reset semantic fields recursively
never memset whole struct
never alter padding
never alter reflected-but-nonsemantic fields
```

D2 supported graph 全部是 POD-like canonical scalars + struct，因此在完整 preflight 和 entry-empty 前提下可以安全 reset 整个 semantic graph，不需要 per-field destructor journal。

## 16. Typed memory access

scalar read/write 必须先使用 typed local value，再通过 `memcpy` 与已经过 storage proof 的 object region 交换。

目的：

```text
avoid strict-aliasing assumptions
avoid direct unaligned typed dereference assumptions
keep conversion fully completed before mutating destination
```

不得用 arbitrary `void *` cast 后直接写未证明 representation 的 scalar。

## 17. BOOL conversion

```text
CSERDE_BOOL -> BOOL  accepted
anything else        CBIND_TOKEN_MISMATCH
```

不允许 numeric/string bool coercion。

## 18. Integer conversion

允许：

```text
SINT -> SINT  if target range contains value
UINT -> SINT  if value <= target signed max
SINT -> UINT  if value >= 0 and <= target unsigned max
UINT -> UINT  if target range contains value
FLOAT -> INT  only if finite + integral + target range
```

任何失败 -> `CBIND_VALUE_OUT_OF_RANGE`，不是 silent truncate/wrap。

float->integer 检查必须避免先执行 out-of-range C cast；先做数学范围/有限性/整数性证明，再 cast。

## 19. Floating conversion

### FLOAT -> FLOAT

```text
binary64 target:
  canonical binary64 direct

binary32 target:
  finite overflow -> CBIND_VALUE_OUT_OF_RANGE
  nonzero value underflows to zero -> CBIND_VALUE_OUT_OF_RANGE
  normal binary32 rounding -> allowed
  NaN/+Inf/-Inf -> allowed
```

### SINT/UINT -> FLOAT

整数必须在目标 binary32/binary64 中 exact representable；否则：

```text
CBIND_VALUE_OUT_OF_RANGE
```

必须覆盖边界：

```text
float32: 2^24 accepted, 2^24+1 rejected
double:  2^53 accepted, 2^53+1 rejected
```

例如 `9007199254740993ULL` 不得静默成为 `9007199254740992.0`。

禁止 string->float/bool->float coercion。

## 20. Struct canonical representation

```text
MAP_BEGIN
  STRING(field-a) value
  STRING(field-b) value
MAP_END
```

CSerde 本身允许 arbitrary canonical MAP keys；CBind struct binding 更严格，key 必须是 STRING。

field lookup 必须对 `cserde_slice` 做 exact length + bytes compare。slice 不保证 NUL，不得为调用 C-string helper 而分配/copy key。

field names case-sensitive。

## 21. Struct decode state machine

```text
1. consume token; require MAP_BEGIN
2. reserve/zero seen bitmap
3. loop:
   a. consume next token
   b. MAP_END -> finish/check missing
   c. otherwise require STRING key
   d. exact semantic field lookup
      unknown   -> CBIND_UNKNOWN_FIELD
      duplicate -> CBIND_DUPLICATE_FIELD
   e. recursively decode child
   f. only after child success mark seen bit
4. at MAP_END verify every semantic field seen
5. first missing field in descriptor order -> CBIND_MISSING_FIELD
6. success -> CBIND_OK
```

field order independent。

D2 所有 semantic fields required；无 optional/default。

zero-field semantic struct：`MAP_BEGIN MAP_END` success，scratch 0 bytes。

## 22. Reader position on binder failure

D2 明确保留 forward-only consumption：

```text
root token mismatch:
  mismatching token consumed

unknown field:
  STRING key consumed
  paired value not consumed

duplicate field:
  duplicate STRING key consumed
  duplicate value not consumed

non-string struct key:
  key token consumed
  paired value not consumed

missing field:
  MAP_END consumed

known field child failure:
  reader remains after exact child prefix consumed before failure
```

调用方不得假设同一个 reader/value 可直接 retry。

## 23. CSerde source-status mapping

如果 `cserde_reader_next()` 在 CBind 仍需要 token 时返回：

```text
CSERDE_DONE
  -> CBIND_UNEXPECTED_END
     error.source_status = CSERDE_DONE

any actual CSerde failure
  -> CBIND_SOURCE_ERROR
     error.source_status = exact failure
```

binder depth/scratch/type/range errors：

```text
error.source_status = CSERDE_OK
```

CBind 不二次解释 CSerde provider failure。

## 24. Internal token-fed machine

为了未来直接接 JSON/YAML SAX push event，D2 implementation 可以把 binding semantics 收敛到 internal token-fed state machine：

```text
cbind_machine_begin
cbind_machine_accept(token)
cbind_machine_finish
```

`cbind_decode()` 是基于 `cserde_reader_next()` 的 generic driver。

machine API 在 D2 仍是 private implementation detail：

```text
not installed
not exported
not ABI promise
```

未来 parser adapter 若需要跨 target 调用 machine，必须先独立设计稳定 integration ABI；不得因为同 repo 就直接 include private internal header 跨 installed-target 边界。

## 25. Parser/CFlow non-goals for D2

D2 不实现：

```text
cbind_json_decode
cbind_yaml_decode
cbind_xml_decode
cbind_csv_decode
CBindFlow
Stream<T>
```

这些属于上位架构的后续 phases。

`CBindFlow` 当前是 architecture/design label；最终 target 名、header 路径与 public fluent API 必须在独立 spec 中确认。`cflow_json(...)`、`cbind_json_array_stream(...)` 等写法只作为概念示例，不构成已批准 ABI。

固定不变量只有：

```text
raw cserde_token is not business Stream<T>
CFlow begins at complete semantic/bound native value boundaries
CBind core does not depend on CFlow
```

## 26. Test-only CSerde source

TurboUtils CSerde 的现有 recording reader/writer support 是 repo-local test-only support，不能假设 TurboParser tests 能 link 它。

D2 tests 在 TurboParser 内提供一个最小 test-only `cserde_reader_ops` fixture：

```text
iterate caller-owned cserde_token array
no heap
no installed/exported header
no production dependency
```

不得为了跨 repo test convenience 把 TurboUtils recording fixture 升格成 production CSerde API。

## 27. Required tests

### Context / ABI

```text
NULL/short/wrong-version context
scratch_size > 0 with NULL scratch
valid zero scratch scalar
malformed error prefix
success error normalization
```

### Scalar

```text
BOOL exact token only
SINT/UINT cross-sign boundaries
native int/long/size_t width boundaries
FLOAT -> integer finite/integral/range
integer -> float exactness
float64 -> float32 overflow
float64 -> float32 nonzero underflow-to-zero
float64 -> float32 normal rounding accepted
NaN/Inf
wrong token kinds
```

### Struct

```text
root struct
nested struct
order-independent fields
empty struct
transient non-NUL key slice
unknown field
duplicate field
missing field
non-string key
known child error propagation
```

### Preflight

```text
unsupported root: zero reader calls
unsupported nested child: zero reader calls
malformed scalar width: zero reader calls
malformed struct storage/layout: zero reader calls
field size/alignment mismatch: zero reader calls
duplicate semantic field metadata: zero reader calls
semantic descriptor cycle: zero reader calls
insufficient max_depth: zero reader calls
insufficient exact scratch: zero reader calls
non-empty destination: zero reader calls
```

### Transaction / reader position

```text
partial success then later field failure -> root semantic graph empty
padding unchanged
nonsemantic reflected fields unchanged
unknown/duplicate key value remains unread
missing MAP_END position exact
source error maps to exact source_status
```

### Portability

```text
C11 public headers
C++17 include/link consumer
Windows long boundary derived from sizeof(long)
Linux native widths
```

## 28. Build / package verification

D2 completion requires fresh exact-head verification：

```text
Linux fresh configure
Linux full build
Linux full CTest
Windows fresh configure
Windows full build
Windows full CTest
selected CMeta/CSerde/CBind tests
installed package consumer: find_package(TurboParser) + TurboParser::CBind
```

installed consumer 必须证明 `TurboParser::CBind` link interface 没有意外传播 Parser/DataBind/TBE/STL/Core/CFlow。

## 29. D2 file shape

计划模块：

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
    support/
      recording_reader.c
      recording_reader.h
    cbind_context_test.c
    cbind_scalar_decode_test.c
    cbind_struct_decode_test.c
    cbind_transaction_test.c
    cbind_header_cpp_test.cpp
```

production files 不 include test support。

## 30. D2 final invariants

```text
CBind is owned by TurboParser repo.
CBind public identity is TurboParser::CBind + <cbind/...> + cbind_*.

CBind core depends only on CMeta + CSerde.
CBind core remains format-neutral.
CBind core remains CFlow-independent.

All unsupported/malformed graph failures happen before reader consumption.
All post-consumption failures restore semantic destination to empty.
Reader is never rewound.

No arbitrary scalar layout guessing.
No silent numeric truncation/rounding outside explicitly allowed float32 conversion.
No struct-wide memset rollback.
No transient key retention.
No mandatory DOM/token queue.

D2 does not modify DataBind/TbeTyped.
D2 does not expose parser adapters or CBindFlow.
```
