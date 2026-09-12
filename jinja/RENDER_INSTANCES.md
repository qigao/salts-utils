# 单次渲染资源与模板实例

## 决策背景与方案

include 在当前 provider 上执行。公开 render 入口创建唯一的
JINJA_CMETA_RENDER_STATE，递归执行不能新建资源所有者。函数、cell 和指令索引
属于各自的编译模板，因此闭包、模板引用和延迟循环状态都保存定义模板实例。

直接递归调用公开 render 会重置访问、分配和递归预算，并在子调用返回时释放逃逸
值依赖的存储。环境级实例缓存则引入共享可变状态和失效协议。当前同步 include
采用渲染内实例所有权，复用同一执行器，不需要环境缓存。

## 所有权与生命周期协议

- 执行为单线程同步调用，loader 回调不得重入。
- 调用方拥有根模板、环境和根数据，直到 render 返回。
- 渲染拥有加载的模板、实例、activation、闭包、节点、集合快照、上下文快照和捕获字符串。
- 每个 activation 借用自己的编译布局；定义父链必须属于同一模板和同一 cell store。
- 切换实例只切换可见上下文、activation 和编译元数据，不复制或重置共享资源。
- include 的上下文快照保留可见局部值；子模板赋值不改变调用方绑定，namespace 引用保留共享身份。
- without context 禁止读取调用方根数据；显式跨实例传递的宏仍保留自己的定义上下文。
- 每次成功加载的 source lease 恰好释放一次，包括校验或编译失败路径。
- 已加载模板保留到渲染结束，保证子模板返回后的宏、self 和延迟循环仍可使用其元数据。
- cleanup 先释放运行时借用者，再释放加载的模板；不释放调用方的根模板和环境。

## 容量、失败与可见行为

现有 render options 继续作为配置入口。节点、activation、字符串、比较访问和调用
深度预算由所有 include、宏和递归循环共享。环境中的加载数量与源码字节上限作为
单次渲染累计预算；重复 include 重新加载并消耗预算，不建立缓存。

为保持单模板容量行为，集合和 cell 工作区仍在入口处计算一次：

~~~text
集合槽数 = max_nodes * max(expression_count + function_count, 2)
cell 槽数 = max_nodes * max(cell_count, 1)
~~~

乘法在分配前检查溢出。子模板消耗同一工作区的剩余槽位，不扩容或搬移已发布存储。
因此入口模板的复杂度仍影响可用容量；本次分离的是存储所有权，没有新增公开配额配置。
集合空间为上述槽数乘以 VALUE 大小，cell 的累计分配受槽数和 activation 数双重约束。
上下文快照沿用有界 O(N²) 名称去重；N 受现有节点与编译布局上限限制。

容量耗尽返回 CAPACITY，分配失败返回 OUT_OF_MEMORY，缺少 loader 返回 LOADER。
候选名称查找和 ignore missing 只处理 NOT_FOUND；其他加载、编译和运行时错误向上传播。
错误保留失败模板的名称，包括合法的空名称。流式输出不回滚，字符串输出失败时丢弃部分结果。

包含 include 或 scoped block 的循环，即使没有显式读取 loop，也保留真实 loop cell。
子模板可据此触发定义模板中的 lookahead 或递归执行。

## 兼容性、迁移与回滚

公开 C 函数签名和配置结构布局保持不变。include/import/from/extends 已接入单次渲染内实际执行。
模板身份与调用实例在渲染内共享，继承关系通过父链延迟执行；include/import/from 使用同一渲染实例与预算池。
按名称加载仍不跨渲染缓存；新的累计加载上限只作用于一次渲染内的依赖加载。

迁移顺序为：为 activation 和值补定义模板身份，分离渲染资源与模板实例，再让 include
复用执行器。回滚必须同时移除 include 执行和对应公开说明，不能保留 include 却恢复
模板全局索引。失败会结束当前渲染并统一清理，无外部数据迁移或新增依赖。

## 验证范围

在 Visual Studio 开发环境中使用仓库 preset：

~~~text
cmake --build --preset win-release-user --target test_jinja_environment test_jinja_native_functions test_jinja_cmeta
ctest --preset win-release-user -R "^test_jinja_(environment|native_functions|cmeta)$" --output-on-failure
~~~

环境测试覆盖逃逸宏、self、上下文隔离、延迟循环 lookahead、递归循环、候选名称、
loader lease、诊断、累计源码/加载/访问预算、include 环，以及未实现引用的显式报错。
native-functions 和 cmeta 测试覆盖相邻 cell、闭包与单模板渲染行为。
