# Jinja 模板加载与跨模板执行接口提案

状态：2026-09-11，用户在接口确认请求后回复“keep going”，按上述接口范围进入实施；
方案批准不等于功能已实现，不关闭 issue #26 的 M4/M5/M6。
已按用户最新约束收简：不设计旧ABI兼容、busy-destroy或回调重入检测；内部依赖正常调用约定。
远程记录：[issue #26 模板组合提案](https://github.com/qigao/salts-utils/issues/26#issuecomment-5611605568)。

## 事实、目标与影响

事实：`jinja/src/parser/jinja_expression_parser.h` 的 JINJA_TEMPLATE_REFERENCE 已表示模板表达式、
with_context、ignore_missing及导入名称；`test_jinja_assignment_parser.c` 已覆盖对应头语法。
方案提出时，`jinja_cmeta.h` 只有源码编译及单模板渲染。2026-09-11已接入下述环境、
命名编译和同步loader接口及NOT_FOUND/LOADER错误；跨模板执行仍未接入。

HIGH，事实：`jinja_cmeta_value.h` 的CLOSURE记录activation/function/autoescape/context，但未记录定义模板；
`jinja_cmeta_provider.c` 的jinja_invoke_function仍从provider->templ取函数表。
`jinja_cmeta_cells.h` 的CELL_STORE也绑定一个模板。跨模板宏不能只借用一个函数索引，
否则可能执行调用者的同索引函数，或使用错误的cell布局；必须保留定义模板及运行实例身份。

当前单模板block/scoped/required/self已接入原生执行器；不等于跨模板继承已实现。
目标：同一个原生执行器支持动态include、import/from及跨模板extends/super。
Parser继续使用re2c/Lemon；运行时不依赖Mustache、QueryVM、文件系统或Python。
此次设计涉及公开API、编译器、运行时及测试四个边界，已确认接口后分步实施。

## 上游依据与可重复证据

- 固定本地源码：`build/jinja-oracle-3.1.6/Lib/site-packages/jinja2/compiler.py` 的
  visit_Include、_import_common、visit_FromImport；environment.py的join_path、_get_default_module。
- [官方加载接口](https://jinja.palletsprojects.com/en/stable/api/#loaders)。
- [官方include与import语义](https://jinja.palletsprojects.com/en/stable/templates/#include)。
- 当前 `jinja/test/oracle/jinja_oracle.py` 固定cache_size=0、auto_reload=False；不能更换配置来缩小实现目标。

事实：新增10条composition_* oracle预期通过，全部573条校验通过。覆盖include上下文隔离、
without context、候选列表、仅忽略查找缺失、候选语法失败、import上下文、无缓存身份、
from缺失导出、跨模板同名宏及可终止递归include。它们尚未成为C实现验收绿测。

复跑：

```powershell
build/jinja-oracle-3.1.6/Scripts/python.exe jinja/test/oracle/jinja_oracle.py jinja/test/oracle/cases.json --verify
```

## 方案比较与选择

1. 在每次render参数追加loader：改动小，但词法配置和模板身份无统一所有者，后续import与继承容易分裂。
2. opaque环境持有不可变词法配置、loader及组合限额，单次执行session持有运行时实例：推荐。
3. 编译时展开全部依赖：不能支持由运行时条件/上下文决定的模板名，排除。

采用同步直接调用和一个薄loader适配边界，不引入插件总线、全局注册表或异步框架。
按现有固定profile不做模板/模块缓存，每次名称加载产生新的编译产物。
未来缓存若被列入目标，必须独立设计失效与模块身份，不得偷偷按名字复用实例。

## 公开环境契约

opaque `JINJA_CMETA_ENV`只提供四个环境操作：

```c
JINJA_CMETA_ENV *jinja_cmeta_env_create(
    const JINJA_CMETA_ENV_OPTIONS *options, JINJA_CMETA_ERROR *error);
void jinja_cmeta_env_destroy(JINJA_CMETA_ENV *env);
JINJA_CMETA_TEMPLATE *jinja_cmeta_env_compile(JINJA_CMETA_ENV *env,
    vstr name, vstr source, JINJA_CMETA_ERROR *error);
JINJA_CMETA_TEMPLATE *jinja_cmeta_env_load(JINJA_CMETA_ENV *env,
    vstr name, JINJA_CMETA_ERROR *error);
```

以上为接口签名；完整可运行调用见`jinja/test/test_jinja_environment.c`及C++头测试。
ENV_OPTIONS包含现有COMPILE_OPTIONS的值、loader回调表及组合限额。
create复制词法字符串和回调表；userdata由调用方持有到destroy。环境创建后不变，不增加版本协商或setter。
env_compile的name是诊断和依赖身份，source仍是显式长度UTF-8；编译结果独占自己的源码派生存储。
env_load调用loader并复用同一编译核心；不执行模板正文或预加载动态依赖。

模板借用env直到jinja_cmeta_release；调用方先结束render并释放模板，再destroy环境。
不维护活跃产物计数或busy状态，也不隐式清理模板。NULL destroy无操作。
现有render/render_string/release继续消费同一种模板。
现有源码compile是无loader的匿名编译能力，不保留第二种执行器或旧语义profile；
执行到依赖加载而没有loader时返回LOADER，不把它当NOT_FOUND。

loader表两个同步回调：

```c
JINJA_CMETA_STATUS (*load)(void *userdata, vstr name,
    JINJA_CMETA_SOURCE *source, JINJA_CMETA_ERROR *error);
void (*release)(void *userdata, JINJA_CMETA_SOURCE *source);
```

SOURCE由`vstr text`与opaque `void *lease`组成。text属于loader，只有load返回OK才获得租约；
此后无论源码验证、编译、分配成功或失败，都必须恰好调用一次release，调用后text立即失效。
非OK时loader负责收回自身部分资源，返回的source不发布，引擎不调用release。
release不能失败、不能回调引擎；它不是用于延长模板字节借用的引用计数入口。
名称是原样UTF-8逻辑键，默认相对loader根；不自动join父路径、不规范化、不内置磁盘读取。
空字符串作为合法键交给loader；UTF-8非法为INVALID_ARGUMENT或运行时RENDER，不能修复后重试。
磁盘路径权限、目录穿越和编码转换由显式宿主适配器负责，不能把任意模板名直接当路径打开。

新增状态：NOT_FOUND=-8、LOADER=-9；保持已有语法/容量/OOM错误。
NOT_FOUND只表示本次请求名称不存在；权限/I/O失败为LOADER，不允许被ignore missing吞掉。
ERROR现保留失败模板名称（256字节缓冲、UTF-8完整前缀、显式长度和截断标志）
及该模板原始字节offset；跨模板接入时message还需给出父引用位置。
不得返回指向已释放子模板或loader租约的view。
这会改变公开错误结构布局，所有调用者必须重编译；不提供旧布局shim。

## 状态、所有权与资源协议

ENV控制配置和loader；单线程同步session是可变运行时的唯一所有者。
每个运行实例有自己的模板、root/context、cell store与词法激活；session拥有稳定地址的实例及共享预算。
跨模板closure保存定义实例身份和函数索引，模板、cell和VALUE payload一直存活到session结束。
调用别的模板宏时切换定义实例，使用调用现场的sink/caller参数，返回后恢复调用者；
不能把词法parent改成动态调用者，也不能让子render新建独立总预算。

with context传递当时可见值的只读名称快照；值按现有身份语义共享，因此namespace变更仍可观察。
不复制/改写宿主CMeta root；子模板set只写自己的root激活，调用方set不被改写。
without context只获得环境内建/已配置globals，不读取调用方root或局部变量。
模块导出属于其运行实例，不能在import返回后销毁模块provider；from缺失导出生成Undefined，
私有名称规则沿用parser；导入的别名不能误加入当前模块的可导出集合。

单个env按单线程、同步且无回调重入的约定使用，不维护检测这些违约调用的状态机。
不同env可独立执行；共享userdata由宿主同步。引擎不增加锁、后台任务或隐式等待。
render失败统一销毁session：先断开运行栈引用，再销毁运行实例/payload，最后释放子模板。
无返回给宿主的运行时VALUE，因此不存在跨session逃逸；sink已交付字节不回滚，render_string失败不发布输出。

新增组合限额只覆盖实际增长的子模板数量和累计源码字节：max_loaded_templates、max_loaded_source_bytes。
模板嵌套复用max_render_depth；实例内节点、VALUE和字符串沿用现有限额，跨模板不重置共享计数。
源码接受后、编译前检查数量与字节；拒绝仍释放本次源码。零配置沿用现有“使用默认值”约定。
不为此次组合功能新建load-attempt、总指令、流式总输出和统一allocator计费框架；这些不是本批前置条件。
编译产物还受原有每模板源码/指令/cell上限约束；满额CAPACITY，不重试或切换执行器。
这不是完整sandbox，宿主loader内部I/O/分配仍由宿主负责。

## 动态加载、递归与错误语义

include候选按原序求值和查找，仅NOT_FOUND继续候选；第一个可加载但语法错误的模板立即失败。
ignore missing只捕获当前include的名称选择失败，不吞子模板执行中的NOT_FOUND或任何其他错误。
include默认with context，import默认without context；模块初始化输出捕获但不直接交付父sink。
固定cache_size=0意味着两个按名字import得到两个实例，不能只因同名就共享namespace或macro身份。

HIGH，事实：composition_include_terminating_recursion输出210，故“活动名字重复立即拒绝”会改变合法Jinja语义。
include和动态import允许重复调用，达到共享模板数量或深度限额时CAPACITY。
extends的祖先链另行检测结构循环并报RENDER；不能拿继承的非环约束套在所有include上。
别名或循环改名同样计入数量/字节预算；不依赖路径canonicalization限制资源。

## 实施依赖与验证门槛

实施步骤在既有语法完成计划中跟踪；禁止安装空env/loader API。

1. 公共头与错误契约、loader源码生命周期：修改jinja/include/jinja_cmeta.h，新增
   jinja/src/jinja_cmeta_environment.c及对应TinyTest；覆盖OK/NOT_FOUND/LOADER、非法source、
   编译失败、配额拒绝和release恰好一次；不为违反销毁顺序或不支持的回调重入建立测试矩阵。
2. 跨模板实例归属和共享session：修改value/cells/provider内部边界；必要时拆出
   jinja_cmeta_session.c，避免继续扩张provider。先验证两个模板同索引宏不混淆、caller跨模板、
   namespace共享、默认参数、失败恢复及session预算，现有宏/LoopContext全部回归。
3. include端到端：复用REFERENCE生成原生指令，在执行时加载；将新增六类include oracle迁为
   公共TinyTest，增加empty candidates、Undefined名称、Unicode/NUL、加载失败与嵌套sink失败。
4. import/from端到端：模块VALUE/导出表及生命周期，验证默认无上下文、with context、
   缺失导出、同名重复导入身份、跨模板宏/闭包/caller、模块正文输出抑制及私有导出。
5. 继承在上述边界上实现：block表、动态extends、super/self、scoped/required、祖先链环与深度；
   不用源码拼接或预展开代替运行时分派。
6. 三配置最小/相邻Jinja回归、Release全仓、ASan、Oracle；新增API后必须install/export及独立
   C/C++ find_package consumer验证。更新M1–M6，不能把方案或私有解析列为公开支持。

迁移影响：新增ENV调用点、ERROR布局及错误switch分支；现有单模板源渲染语义不需要兼容分支。
测试影响跨parser/compiler/provider及下游消费；无新第三方依赖、配置文件或部署方式。
回滚仅反向撤回本批接口/实现/测试和矩阵声明，不覆盖已有宏工作或其他脏改动，不迁移用户数据。

## 已确认范围

采用“opaque环境 + 同步load/release租约 + session归属跨模板实例”，
新增上述公开环境接口、组合预算以及模板名错误上下文；不增加兼容层或违约调用状态机。
