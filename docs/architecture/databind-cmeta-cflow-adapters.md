# DataBind CMeta / CFlow 适配层

## 背景

SaltsUtils 的 DataBind 组件以不可变 `DataBindValue` 树提供动态数据访问，并另有 callback 驱动的增量流接口。CMeta、CFlow 与 Reactive 提供类型描述、同步 range、流构造、按需拉取和取消协议；适配层将这些生命周期协议与格式解析实现分开。

## 决策

DataBind 适配 API 由 SaltsUtils 提供，消费者统一链接唯一公开目标 `Salts::DataBind`，不组装内部适配 targets。内部职责分为：

- CMeta 适配：把不可变的 LIST/SET、OBJECT、MAP 暴露为 `cmeta_range`。
- CFlow 适配：复用 CFlow 的 range factory，从同一适配 range 创建 `cflow_stream` 或 Reactive `cflow_publisher`。

适配器输出三种小型平凡值：value ref、field ref、map-entry ref。它们只借用根 `DataBindValue` 及其内部字符串，不取得所有权。CMeta `Schema/Replay` 生成对应的稳定语义 identity、type descriptor 和公开 descriptor getter，避免三份元数据声明漂移。

## 数据与生命周期协议

- 数据单元：包含 `const DataBindValue *`，以及可选 `const char *` 名称/键的平凡引用结构。
- 主事实源：调用方持有的不可变 `DataBindValue` 根对象；range、stream 和 publisher 只保存遍历位置。
- 所有权：调用方必须让根对象存活到 range 遍历结束，或 stream/publisher/subscription 完全关闭之后；适配器销毁不释放根对象。
- 失效点：释放根对象会立即使全部适配值及名称/键指针失效；不得跨该边界保存借用引用。
- 线程拓扑：range cursor 为单线程；stream/publisher 依赖 CFlow 对 resume 的串行化约束，不额外声明线程安全。
- 顺序：保持 DataBind 原始 encounter/schema order。
- 容量与背压：零预取、零队列、零额外 payload buffer；Reactive publisher 只在正 demand 下拉取下一项，terminal 不消耗 demand。
- 错误：kind 与根值类型不匹配、参数无效时 fail fast，输出保持零状态；CFlow factory 失败转换为 `DATA_BIND_ERR_RUNTIME`。
- 关闭：取消或关闭只销毁 CFlow cursor 状态；根对象仍由调用方释放。

## 候选方案

1. **把 CMeta/CFlow 直接编入 DataBind 核心库**：调用更短，但会让所有用户承担新依赖和 ABI 面，否决。
2. **把 callback 增量解析器直接包装为 Reactive publisher**：可支持边解析边消费，但解析回调、暂停、错误和取消目前没有统一的 demand/close 状态机，容易形成双事实源，留待独立设计。
3. **先适配已完成的不可变树**：生命周期和顺序可证明、无需缓存，且可复用 CFlow 已验证的 range publisher，采用。

## 接口与兼容性

适配 API 不修改既有枚举值、数据格式或 one-shot/stream 解析语义。应用通过显式选定的 SaltsUtils 安装链接 `Salts::DataBind`，内部依赖由 SaltsUtils 封装；不提供独立 DataBind package/root、替代 target 拼写或兼容 alias。

## 验证范围

- 截断版本化配置不得访问声明 size 之外字段。
- 三种 CMeta range 的类型约束、顺序、长度、终止状态和借用引用。
- CFlow stream 的 source-only 求值，以及 Reactive publisher 的逐项 demand、terminal 和 cancel/close。
- C 与 C++ 头文件消费、Windows shared-library 导出、安装 target 依赖。
- 全部既有 DataBind 测试回归。
