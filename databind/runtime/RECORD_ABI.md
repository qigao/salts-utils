# DataBind 3.0 ABI

DataBind 使用纯 C schema parser、动态值树和 canonical CMeta/native binding。运行时不加载
中间代码、不生成机器码，也不要求宿主进程提供编译器。

## 版本边界

- library version: `3.0.0`
- C ABI: `10`
- schema codec provider ABI: `tbe_schema_codec_v1_t`
- public datetime/query diagnostic types are DataBind-owned and do not expose SaltsUtils parser/query headers
- concrete parser/query implementations remain private implementation dependencies of the SaltsUtils DataBind component
- 旧的运行时 IR、缓存和产物加载接口已删除，不提供兼容层
- 动态 kind 到 CMeta kind 的迁移接口 `data_bind_cmeta_data_kind` 已删除；
  schema 语义直接读取 `DataBindSchemaField.cmeta_kind`，native 语义直接读取
  canonical CMeta descriptor，不再从动态值标签推导原生类型
- BinaryLayoutPlan 只接受 ABI `2` 与完整的当前记录，旧 provider 必须重新生成
- opaque codec/object 持有解析后 schema fingerprint。对象序列化拒绝不同 schema，
  `DataBindRecord` 文本构造器拒绝非 object 根

调用方必须在加载动态库后检查 `data_bind_abi_version()`。ABI 不等于
`DATA_BIND_ABI_VERSION` 时应立即拒绝使用，不能继续解析或释放跨 ABI 对象。

## 对象与所有权

- `DataBind` 持有解析后的 schema AST；`data_bind_free()` 释放它。
- `DataBindValue`、`DataBindObject` 和 `DataBindRecord` 是拥有型结果，不借用
  schema AST；按各自的 `*_free()` API 释放。
- `DataBindFieldView` 等 view 只借用所属 record；record 释放后 view 立即失效。
- 文本和二进制序列化结果必须使用 DataBind 对应的释放函数，不跨 CRT 直接
  `free()`。

## 两条强类型路线

1. schema 生成 `.h/.c`：生成代码公开 owning struct、CMeta graph/native binding 和格式
   入口。生成源可编译进静态库或动态库。
2. schema 映射现有 C struct：应用声明 canonical CMeta graph 与
   `DataBindNativeTypeBinding`，编译 MessagePlan 与格式计划，不生成业务头文件。

两条路线都以 schema 为格式与名称的唯一事实源。`[name]` 和 `[alias]` 决定
外部名称与反序列化别名；`[c]` 决定生成代码的成员名，CMeta metadata 则直接
描述现有 struct 成员及生命周期。历史 typed runtime 的公开接口已删除；旧调用方
必须迁移，不提供兼容 alias。

## 空消息与无参 Service

零字段消息是一个完整的逻辑值。C11 和 C++17 生成器统一以一个
`uint8_t _empty_storage` 成员提供原生存储，`sizeof` 为 1；该成员不进入
CMeta 逻辑字段、schema、名称映射或序列化结果。CMeta 逻辑字段数为零，
不生成零长度 C 数组。字段非空的既有 struct 布局保持不变。

Native IR 的字段投影只描述非空字段集合。空消息使用既有的 canonical 完整
Struct 值描述，shape 中字段数为零；它不生成字段投影或状态 overlay facade。
候选方案是扩展 CMeta 的字段投影准入，但现有完整值描述已经能准确表示单位值，
采用它无需改变依赖版本或跨库 ABI。非空记录继续使用已有字段投影。metadata
仍由调用方在最终地址绑定并持有，计划释放前不得移动它。

MessagePlan 按零字段 MAP 解码和编码空消息。原生存储、每次执行的 workspace
及结果仍由调用方拥有；不可变计划可以共享，执行上下文和 workspace 分开。
绑定 metadata 的借用期、关闭和释放规则与其他消息相同；空消息不引入资源 owner。

Service 的 request IN / response OUT 根参数按消息类型认领一次，与字段数量
无关。因此 Empty→Empty、Empty→Status、Status→Empty 均使用既有的生成函数
ABI；typed error envelope 继续占用其原有 OUT 参数。真正未认领的参数仍失败，
不将其作为隐式 context。空消息保留消息语义，既有 `void` response 契约不变。

这项变更不增加 public ABI 记录或版本分发。曾生成空 struct 的 schema 应重新
生成并编译；不需要添加业务字段、兼容类型或修改 wire schema。

## 生成动态库

生成头定义 `TBE_GENERATED_API`：

- 构建 Windows DLL 或 ELF shared object 时定义 `TBE_GENERATED_BUILD_SHARED`。
- 使用 Windows DLL 时定义 `TBE_GENERATED_USE_SHARED`。
- 构建或使用静态库时无需定义二者。

生成库通过 SaltsUtils 包链接唯一公开目标 `Salts::DataBind`；它不包含 C 编译器，也不在运行时编译
schema。

## RulesForge/TurboScript 集成

运行时可直接保存 `DataBind *`，调用动态 `DataBindObject` API；这种方式只部署
schema 和 DataBind 动态/静态库。若需要 `order.id` 一类原生 C 成员访问，则在
RulesForge/TurboScript 的构建或发布阶段生成并编译 schema library，运行时加载
已构建的 library 和 `tbe_schema_codec_v1_t` provider。编译器是构建工具，不是
运行时依赖。
