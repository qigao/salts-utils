# DataBind Native Reader v1：消费方契约

日期：2026-09-21。关联 #99、已合并 #100/#102/#105、TurboDB #52。
状态：按已批准 DataBind-only 方向选择首批实现契约并提交消费方测试；
**尚无生产头文件或实现，未发布新公共 ABI，未取得解码行为 RED/GREEN。**

基线：#105 merge `2ceba0f6c46914600960bc410f5b5453911322ec`，tree 与已验证
`1070396b88516b54ddb189ff19adc433d038d5c9` 完全相同；Salts CI pin 仍为
`801202e58c2d86b35202414d4812e79a2fd25bae`。

## 1. 接口形状与归属

目标公开头：`data_bind_native.h`；实现归属现有 `Salts::DataBindCore`。
以下声明是实现约束，不是当前可链接符号。不得用测试头、空函数、CBind转发或
feature fallback 将接口缺失变成假通过。生产头必须与真正实现一并交付。

```c
enum { DATA_BIND_NATIVE_ABI_VERSION = 1u };
typedef struct DataBindNativeOptions {
    size_t size;
    uint32_t abi_version;
    void *workspace;
    size_t workspace_bytes;
    size_t max_depth;
    size_t max_items;
    size_t max_owned_bytes;
} DataBindNativeOptions;

typedef struct DataBindNativeDiagnostic {
    size_t size;
    uint32_t abi_version;
    DataBindError error;
    cserde_status source_status;
} DataBindNativeDiagnostic;

DataBindStatus data_bind_native_decode(
    const DataBindNativeOptions *options,
    const cmeta_data_desc *shape,
    cserde_reader *reader,
    void *destination,
    size_t destination_bytes,
    DataBindNativeDiagnostic *diagnostic);
```

`DATA_BIND_NATIVE_OPTIONS_INIT` 初始化完整 size、v1 版本，其余为零，要求调用方
显式设置 workspace 和预算；不是“零表示无限”。`DATA_BIND_NATIVE_DIAGNOSTIC_INIT`
初始化完整 size、v1、`DATA_BIND_ERROR_INIT` 和 `CSERDE_OK`。

不修改 `DataBindError`、`DataBindStatus` 的既有布局/枚举值，也不把全库 ABI9 偷改。
诊断只是复用既有 DataBindError 并附带原始 reader status，不创建竞争的错误系统。
头文件提供 C linkage，C++17 调用相同函数。v1 不接受可变长参数或隐藏全局上下文。

## 2. 所有权与原子性

调用方持有 reader、CMeta graph、workspace 和 destination；DataBind 都不释放。
对象地址和容量必须有效且满足对应 native alignment；descriptor 及其子描述在调用
期间不可变。调用方不能让 reader/options/diagnostic/descriptor 与输出或工作区别名。
实现必须显式拒绝 workspace/output 区间重叠，且在任何写入之前检查容量和溢出。

目标必须已经被调用方初始化为类型定义的 semantic-zero，不能是任意未初始化字节。
标量按规范的类型零值判断，Struct 逐字段判断，拥有型 buffer 使用 provider is_zero；
不 memcmp padding、不用 memset 替代 provider 初始化。非空目标在读入之前拒绝，
这一入口不隐式 replace 或 clear 调用方已有对象。

workspace 承载临时 root、遍历工作区和字段出现记录。实现只能在全部读取/校验成功后
使用经过预检的 move/commit 生命周期发布，失败清理所有已构造临时字段，保持目标不变。
provider 的 move 必须满足既有 no-fail/semantic-zero 契约；不能安全发布的形态预先拒绝。
对支持的标量按其真实 C 类型写入，不按相同字节宽度偷换 native 类型身份。

没有 reader close/cancel、数据库锁/事务/WAIT、隐藏 rewind 或消费后的重试。
每个同时进行的 decode 必须使用独立 workspace；本入口是同步调用，不跨线程共享状态。

## 3. 读取与字段策略

成功恰好消费一个完整 value，完成根标量或 MAP_END 后立即停止，不探测下一 token/EOF。
预检失败没有 reader callback；消费中错误停在第一个已知失败处，不自动 drain 或 rewind。
诊断 source_status 保留实际 reader 返回值；例如截断时 reader 返回 DONE，则保留 DONE，
DataBind 返回 PARSE，不伪造来源已经返回 UNEXPECTED_END。

无 overlay 的 Struct 使用 canonical CMeta 字段名，字段顺序任意，所有字段必填。
重复 key 在读到第二个 key 后报 PARSE；未知 key 报 TYPE_MISMATCH 且不读其 value；
缺失字段在 MAP_END 报 TYPE_MISMATCH。不隐式补默认值、不跳过未知字段。
可选/alias/default/presence 策略仍属于后续显式 overlay 阶段，不能伪装已完成。

canonical BOOL 只接受 Boolean token，数字范围按目标类型检查；不沿用 JSON 文本入口的
字符串 coercion。原有格式 facade 的明确格式规则保持原样，不能因新增直接入口倒退。

STRING/BYTES 通过 owned buffer provider 在下一次 reader callback 前复制。
STABLE 也不是永久存活保证；首批契约不发布没有独立寿命的借用结果。
后续每个被支持的 enum/container/optional/fixed/custom 形态必须有明确能力/测试矩阵；
不能从这批 scalar/Struct/buffer 测试推导整个 #99 已完成。

## 4. 预算与状态

- workspace_bytes 限制调用方提供的临时存储；不另建手写 arena、隐藏 heap root 或无界栈。
  不足时返回 LIMIT。实现应复用现有受预算工作区能力，不新增独立内存管理框架。
- max_depth 的根层数为1；零预算在读取前返回 LIMIT。预检需限制/检测描述图深度和循环。
- max_items 限制一次根值中的值节点数（根/嵌套容器也各计1，map key 不单计），并给
  描述图预检独立同值上限，防止扫描本身无界。不得因值尚未读到就预分配无界工作区。
- max_owned_bytes 限制本次结果的累计逻辑 owned payload；零允许空 payload，不允许非空。
  这不是任意 provider 的总物理堆用量证明。provider 自身元数据、终止符与分配策略必须
  在能力矩阵中单独审查/记录，不能以逻辑长度代替实际allocator或最大内存证据。

选定错误映射：无效版本/短options/地址/输出容量/非空目标/重叠 -> INVALID_ARG；
无效或不支持的描述 -> SCHEMA；token 类型/数值范围 -> TYPE_MISMATCH（诊断区分原因）；
截断/错误结构/重复字段 -> PARSE；源 SOURCE_ERROR -> IO 并保留源 status；
配置上限 -> LIMIT；分配失败 -> OOM；违反已承诺生命周期契约 -> RUNTIME。
来源自身的 VALUE_OUT_OF_RANGE/LIMIT/UNSUPPORTED 等状态同样必须保留并明确映射，
不能将所有 reader error 归为数据库 connection loss。

诊断为可选输出；有效记录在每次入口重置，不带入旧错误。无效诊断记录不能越界写入。
诊断文本在返回前复制到 DataBindError，不能借用 cursor 或下一回调会失效的 message。

## 5. 本批可执行消费者

`native_reader_contract_test.c` 新增21个 TinyTest 用例，直接包含生产头并调用生产符号，
覆盖两次根值、schema-free row、预检零读取、版本/容量/重叠、非空目标、数值范围、
native bool、源错、截断/晚字段失败回滚、workspace复用、payload/depth预算、重复/未知/
缺失字段、owned transient bytes、无隐式文本布尔转换、无效descriptor。

`native_reader_cpp_contract.cpp` 是独立 C++17 编译/链接/运行消费者：检查两个新记录的
C layout 和函数签名，实际解码一个 native int。它是另一个 CTest 目标，不是 TinyTest
中的第22例。两个目标只显式链接 DataBindCore/Salts基础，C目标额外链接TinyTest。
真正的运行时闭包仍须链接成功后检查；只看 target 列表不能宣称无parser依赖。

原有18个 storage 与9个 reader-source 控制及其源码均不改动。本批复用 reader_probe，
但不在fixture内完成转换/rollback/目标初始化策略。after_each 只清理实际测试输出，
不能替生产解码器清理隐藏的 staging 泄漏。

## 6. 证据和下一实现边界

本地在来自已验证CI的完整 source archive上执行，C11/C++17消费方都在
`#include "data_bind_native.h"` 处 exit1：真实生产头缺失，属于接口缺失，不是运行时RED。
另在仓库外使用纯声明的临时设计头，以及现有Salts包的真实公共头，对两个测试作
GCC/Clang C11及G++/Clang++ C++17严格syntax-only检查，均exit0。这个临时头没有实现、
不发布、不参与CI；该检查不代表真实库编译、链接、sanitizer或Windows已通过。

容器不能解析github.com，codegraph/rg.exe原生程序不可用；前者回退到已知源码阅读，
后者用仓库外指向系统ripgrep的rg.exe别名。没有声称完整消费者调用图或完整SDK构建。

下一步必须在DataBind既有转换/生命周期主实现中归并graph-only预检、临时构造和直接
reader消费，不复制CBind，不加另一引擎，不通过制造overlay或强制DataBindValue中转。
正式生产header和真实实现共同提交；在全部接口/行为/生命周期/闭包/平台门槛前保持Draft。
本测试提交预期使分支构建显式报缺少入口，不能在这个状态合入master。
