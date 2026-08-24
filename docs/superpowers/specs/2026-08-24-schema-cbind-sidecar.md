# Schema-Generated CBind Sidecar Design

日期：2026-08-24

状态：Accepted for implementation

## 背景

TurboParser 已能把 JSON DOM 投影为 CSerde token，`tbe_compiler` 也能从同一份
schema 生成 owning C struct 与 `TbeTypedType`。目前缺少的是 semantic sidecar：
调用者仍需手写 `cmeta_struct_desc`、`cmeta_data_desc`，才能把 CSerde reader 交给
TurboUtils CBind。

TBE 的 wire offset、endianness、presence bitmap、fixed block、group 与 variable data
属于 wire/layout metadata，继续由 `TbeTypedType` 独占；不得复制进通用 CMeta
semantic descriptor。

## 目标

- schema 仍是字段名、C member 映射与类型的唯一事实源。
- `tbe_compiler` 在构建期生成独立的 CBind semantic sidecar。
- 生成代码直接暴露每个 record 的 `cmeta_data_desc` 和 CSerde decode 入口。
- 既有生成命令与生成文件保持不变；CBind sidecar 必须显式 opt-in。
- 当前 CBind 无法精确表达的 schema 语义在生成期 fail fast。

## 候选方案

### 方案 A：扩展 `TbeTypedType` 为 CMeta descriptor

拒绝。`TbeTypedType` 同时携带 wire/layout 信息；把它解释为通用 semantic metadata
会混淆职责，并使 CBind 依赖 TurboParser/TBE。

### 方案 B：运行期从 schema 构造 CMeta graph

拒绝。它保留运行期 schema 解析与 metadata 分配，不能满足构建期生成，并产生
第二套运行期生命周期。

### 方案 C：由同一 AST 生成独立 sidecar

采用。编译器复用已经解析和注解的 schema AST，另行渲染 CMeta/CBind source。
TBE layout descriptor 与 CMeta semantic descriptor彼此独立，但都由 schema 生成。

## 公开接口

新增 opt-in CLI：

```text
tbe_compiler schema.tbe --lang c \
  --output generated.h \
  --source-output generated.c \
  --cbind-output generated_cbind.c
```

`--cbind-output` 仅支持内置 C generator，并要求 `--source-output`，因为 owning C
record 定义由 typed source 模式启用。生成 header 对每个 composite/group/message
声明：

```c
const cmeta_data_desc *Order_cbind_data(void);
cbind_status Order_from_cserde(cbind_context *context,
                               cserde_reader *reader,
                               Order_t *object,
                               cbind_error *error);
```

`Order_from_cserde` 不拥有 context、reader 或 object。object 必须已通过
`Order_init` 进入 semantic zero；成功后由 `Order_clear` 释放。CBind 的 unknown、
duplicate、missing、range、depth、container 与 buffer limit 错误原样返回。

## v1 支持矩阵

首版仅生成 CBind 已能在当前 ABI 下严格验证和恢复的 storage：

- `int32`/`int32_t`：映射 `cmeta_data_int`，生成 C11 size/alignment 静态断言。
- `int64`/`int64_t`：映射 `cmeta_data_long`，生成平台静态断言。
- `uint64`/`uint64_t`：映射 `cmeta_data_size`，生成平台静态断言。
- `float`、`double`。
- owning `string` (`tstr`)。
- 由上述字段递归组成的 composite/group/message。
- `[name(...)]` 决定 CSerde map key；`[c(...)]` 只决定 C member offset。

以下语义在生成期拒绝，不生成部分可用 API：

- `[alias(...)]`：CMeta field v1 只有一个 semantic name；alias 属于 parser/schema
  key policy，需要后续 schema-aware CSerde projector。
- optional：CBind field v1 没有 presence policy，且不能维护 TBE presence bitmap。
- `bool`：当前 typed C storage 是 `uint8_t`，不是 CMeta canonical `bool`。
- `int8/uint8/int16/uint16/uint32`：CBind 尚无对应 canonical storage descriptor。
- enum、uuid、bytes、fixed array、list、set、map、group collection 与 union。

平台静态断言是 ABI 防线：例如 LLP64 平台不能把 `int64_t` 当作 `long`。生成器
能判断 schema 语义，却不能假定目标 C ABI；不满足映射时编译明确失败。

## 状态与所有权

- schema AST 是生成时唯一事实源，生成后不保留。
- sidecar descriptor 是 immutable static storage，无销毁操作、无全局可变状态。
- decoded object 由调用者拥有；CBind 按现有事务语义在失败时恢复 semantic zero。
- JSON DOM 与 CSerde reader 的所有权仍遵循 JSON adapter 契约，sidecar 不持有它们。

## 依赖与兼容性

- 默认生成路径不新增 CBind header 或 link dependency。
- 只有启用 `--cbind-output` 的消费者需要 `TurboUtils::CBind`；字符串 descriptor
  同时使用 TurboUtils 的 `tstr` CMeta buffer adapter。
- 不修改现有 `TbeTypedType` ABI，不改变已有 `Type_from_json` 行为。
- 生成模板加入 tbe_compiler 安装资源清单。

## 验证

- 编译器单测验证 opt-in 输出、`[name]`/`[c]`、unsupported fail-fast 与参数约束。
- 生成代码测试从 schema 生成 nested record，并以 JSON CSerde reader + CBind 填充
  owning C struct；同时覆盖 string lifecycle、错误回滚与 descriptor 有效性。
- 运行 tbe_compiler、JSON reader、TurboParser facade 的相邻回归测试。

## 迁移与回滚

该功能为独立 opt-in 文件，无现有调用者迁移。回滚只需停止传入
`--cbind-output`；已有 typed C source 与 runtime schema API 不受影响。后续 CBind
增加 fixed-width、optional 或 container 能力时，按支持矩阵逐项解禁，不改变现有
生成函数签名。
