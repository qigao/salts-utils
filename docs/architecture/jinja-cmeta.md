# Jinja CMeta 设计与语法契约

## 2026-09-09 原生执行决策（issue #26，已实现）

本节覆盖下文历史 lowering 与 `legacy-subset-v1` 设计。用户明确要求两个模块独立，
Jinja 默认不转义，不保留 Mustache 兼容执行路径。公开输出接口已独立；本次替换内部程序。

- **HIGH / 事实**：二次解析会把 `{ {#- gap #}{ user.name }}` 拼成变量；
  `{% if users %}` 对两个元素执行两次正文。两个 TinyTest 已复现；前者的字面量输出
  已由本地固定 Jinja 3.1.6 验证。这影响普通文本与条件语义，不能靠输出转义修复。
- 选择单向 Parser → 原生指令 → 现有表达式/值运行时分层。指令为 TEXT、OUTPUT、
  TEST、JUMP、FOR_BEGIN、FOR_NEXT；跳转使用指令索引，表达式使用节点索引。
  不复制 Mustache 解析器，不把生成的字符串当模板再次解析。QueryVM 的显式指令与跳转
  仅作参考；其查询值与模板循环上下文不同，不新增 QueryVM 链接。
- 替代方案：继续 lowering 无法满足独立性；抽象两个引擎为共同 runtime 会把不同的
  truthiness、上下文与转义规则耦合；直接生成指令复用当前表达式 evaluator，改动边界最清楚。
- 编译状态由单线程 builder 持有：有界 CSTL typed Vec 保存指令，tstr 保存字节池，
  指令只存 offset/length，不跨 append 保存地址。最多 65536 条指令、32 MiB 字节池；
  现有输入 16 MiB、块深度 64、表达式 64 上限保持。每次追加检查容量，满额 CAPACITY，
  分配失败 OUT_OF_MEMORY。成功后复制到模板独占的精确长度不可变存储；失败统一释放。
- 每次 render 独占节点工作区、程序计数器与至多 64 层循环栈；模板可只读共享，release
  必须等待所有 render 返回。FOR_BEGIN 求值一次，FOR_NEXT 消费同一迭代对象；else 只在
  首次无元素时执行。TEST 不压入对象上下文、不遍历 sequence，也不为 else 重算条件。
- 输出同步且有序，回调失败即停止；流式输出不回滚，字符串输出失败丢弃部分结果。
  无队列、跨线程状态迁移、后台关闭或阻塞背压。错误由公开 render 边界报告状态与指令源偏移。
- 编译新增 O(指令数 + 字节数) 存储/复制；执行每条控制指令 O(1)，表达式与迭代成本另计。
  不宣称性能收益。公开函数签名保持；内部模板不持久化，无数据迁移。新增指令限额与修正
  的条件语义是可见变化，以边界、嵌套/elif/for-else、单次消费、回调失败和现有回归验证。
- 顺序：先失败测试，再同时切换编译器与执行器，最后移除私有 Mustache 头/链接并验证
  MSVC Release、Debug+ASan、Clang、完整 CTest、安装与外部消费者。失败时修正新路径，
  不加入运行时 fallback；必要时整体撤回本次局部 patch，不回滚用户其他修改。

## 共享标签扫描边界（issue #26）

公开编译器与私有模板树复用 `jinja_template_scan_tag`，统一引号、括号、注释、
raw/endraw 和左右空白控制的扫描规则。公开编译仍逐标签生成原生指令，不创建整棵
模板树；私有树现已改为按需CSTL存储，不再限制256节点。原生指令仍受65536条上限约束。
普通文本 token 只推进扫描位置，由编译器按源 span 收集，不能交给标签入口。

扫描结果仅在成功时发布；span 借用不可变源码，lexer 由当前编译调用独占。
失败后放弃当前扫描并走编译器统一清理。此批不新增公开 API 或模块依赖。
公开入口已支持 statement 的 `+` 标记，以及插值和注释 `-` 对 Unicode 空白的裁剪。
后续公开环境选项接入见下节；本节记录共享扫描器前置批次。

验证事实：MSVC Release CTest 44/44，Clang/ASan Jinja 各 4/4；
新增空白控制表中的 4 项与固定 Jinja 参考实现一致；300 段 raw 精确长度输入编译通过。

## 公开编译选项（issue #26）

用户批准直接迁移为 `jinja_cmeta_compile(source, options, error)`，移除二参数签名。
选项包括六个定界符、两个行前缀、trim/lstrip/keep与LF/CRLF/CR输出换行。
NULL选项使用初始化器默认值，零填充结构非法。公开头、C/C++测试、benchmark及SDK
消费程序同步迁移；这是源码与ABI破坏性变更，所有调用方必须重编译，不设兼容包装。

Parser仍按原始UTF-8源码扫描并保存字节偏移。共享配置验证及content投影函数，
原生compiler逐标签处理，不创建私有整树。右侧吞白同时推进lexer，避免被吞掉的
缩进参与后续定界符或行前缀匹配；显式`+`覆盖全局策略。原始cursor保留给诊断。

选项由调用方拥有且调用期间不可变，表达式builder只借用本次newline_sequence；
成功时文本及解码常量复制至模板独占存储，失败走既有清理。无全局状态、跨线程写入、
运行时新增依赖或持久化迁移。字面文本按物理换行转换，字符串先转换物理换行再解码，
显式转义与应用字段不变换。扫描O(源码字节数)，文本沿用32MiB程序预算，
解码常量沿用16MiB预算，扩张前检查；不宣称性能改善。

默认行为改变为移除末尾一个物理换行并输出LF。测试覆盖自定义及默认配置隔离、
CRLF原偏移、-/+与raw/注释边界、三种换行和转义/常量比较、源及配置存储失效后渲染。
审查发现的词法推进问题已由失败测试复现后修正。运行时宏、加载、继承及注册表不在本批完成范围。

## raw 词法区域与显式长度边界（issue #26）

raw/endraw 在编译阶段产生 TEXT，不进入表达式或控制块解析。re2c 的 raw 入口只识别
`{%`，避免普通入口先消费 `{{` 而遗漏 `{{% endraw %}` 的重叠结束标签。
首个合法 endraw 关闭区域，内部 raw opener 是普通文本；结束标签检测与 Unicode
空白裁剪复用 Unicode scalar scanner，并补 Python U+001C–U+001F 空白定义。
扫描和裁剪为 O(n)，无额外可增长容器；输出仍由有界程序字节池拥有。
借用输入仅在 compile 期间有效，成功后文本已复制；失败走原有统一清理。

HIGH / 事实：精确长度 heap 输入 `{% raw %}x` 在原 sentinel 模式下由 ASan 复现
尾后字节读取。普通/raw 两入口改用 re2c custom API，YYPEEK 在读取前检查 limit，
不新增输入 padding 或修改公开 vstr 契约。表达式 parser 已复制到独立 terminated buffer。
参考 [re2c 输入边界与 generic API](https://re2c.org/manual/manual_c.html)。

验证事实：247 TinyTest / 1099 断言，Release CTest 42/42，ASan 与 Clang 的 Jinja
CTest 各 2/2；固定上游 oracle 258 项通过（不作为 C 实现覆盖率）。本轮未提交源码。

## 引号与括号感知的标签边界（issue #26）

普通变量/语句标签在 re2c token spans 上跟踪引号、反斜杠转义和有界括号栈。
只有引号外且括号栈为空时识别结束定界符；逐字节定位允许 dict 的结束括号与
`}}` 跨 token 重叠。注释仍在首个 `#}` 结束，raw 使用独立入口。
状态仅属于当前编译调用，空间 O(表达式深度上限)、扫描 O(标签字节数)，不分配新容器。
括号栈上限与表达式节点上限一致，溢出 CAPACITY，未结束/不匹配 SYNTAX，错误偏移为 opener。
无公开接口或依赖变化；验证覆盖引号内定界符、转义单双引号、紧邻 dict、注释和精确长度坏输入。

## 最初实现的历史背景（以下不代表当前执行架构）

`build/Jinja2Cpp-master` 证明了 Jinja 风格模板在本地代码生成中的用途，但其 C++
对象模型、AST、表达式运行时和第三方依赖不适合直接进入本 C11 仓库。现有
`Salts::Mustache` 已提供经过规范测试的模板编译、上下文栈、转义输出和有界展开。

完整的版本化 feature matrix、Jinja 3.1.6 oracle 配置、Jinja2Cpp 测试场景映射与
原生 re2c/Lemon parser 决策见 [`jinja/JINJA_COMPATIBILITY.md`](../../jinja/JINJA_COMPATIBILITY.md)。
以下保留最初实现的设计背景；当前执行架构以上节为准，当前功能范围以 feature matrix 为准，
不存在可选择的 legacy profile，也不得据此宣称完整 Jinja 兼容。

扩展运行时由环境持有 immutable registry snapshot：function、filter、test 与 scalar global
分别按命名空间查找。filter/test callback 的位置参数 0 是被处理值，generic callable 同时
可通过参数中的 `cmeta_data_desc` 与 object 借用视图访问复杂 CMeta 数据，借用仅在 callback
调用期间有效。named loader 结果进入有界 compiled-template cache，cache 只保留 compiled
artifact；render-local instance、上下文、宏/循环状态和预算仍由一次 render 独立拥有。
缓存达到 `MAX_CACHED_TEMPLATES` 后按 FIFO 淘汰；淘汰只释放环境持有的引用，不会破坏仍被
调用方持有的 compiled template。

独立顶层模块 `jinja/` 提供 `Salts::JinjaCMeta`：把有限的 Jinja 控制语法编译为
Mustache，并通过 `cmeta_data_desc` 读取调用方的不可变 C 数据。它不是 Jinja2 的完整
兼容实现，也不属于 `mustache/` 模块。

## 公开语法契约

支持：

- `{{ path.to.value }}` 插值；路径仅由 C 标识符片段组成，可用平衡括号分组。插值也支持
  `true` / `false` / `True` / `False`、`none` / `None`、有符号十进制或 `0b`/`0o`/`0x` 前缀
  `int64`、十进制/指数 double 和受限单/双引号 UTF-8 字符串；字面量可
  分组或重复前置 `not`。数值支持一元 `+` / `-` 与二元 `+`、`-`、`*`、`/`、`//`、`%`、`**`。
  点路径、布尔、整数、浮点与字符串叶节点还支持 `==`、`!=`、`<`、`<=`、`>`、`>=`，
  并可用 `in` / `not in`、`and` / `or` 组合，也支持 `value if condition else fallback`
  和省略 `else` 的条件表达式；
  还支持有界 list、tuple、dict 字面量：可嵌套、允许 trailing comma，并参与 repr、truthiness、
  lookup、membership、比较、类型 test 与循环；
  也支持 `is` / `is not` 的零参数 `defined`、`undefined`、`none`、`boolean`、`true`、
  `false`、`integer`、`float`、`number`、`string`、`mapping`、`sequence`、`iterable`；
  布尔按 `0/1` 参与数值比较。布尔表达式输出采用 Jinja 的 `True` / `False`，整数输出采用十进制，
  浮点输出采用 locale-independent shortest-round-trip 格式。
- 后缀 `[]` 可链式组合：string key 读取 CMeta struct；bool/`int64` key 读取 borrowed sequence
  或 UTF-8 string，负索引从尾部计数。字符串按 Unicode scalar 边界返回原始 UTF-8 subview。
- `{{ path | escape }}`、`{{ path | e }}` 显式 HTML 转义，以及
  `{{ path | safe }}` 原样输出；每次插值最多一个 filter。
- `{# comment #}` 注释。
- `{% if path %}`、`{% elif path %}`、`{% else %}`、`{% endif %}`；`if` 和
  `elif` 条件允许点路径、两种 Jinja 大小写布尔、`none` / `None`、有符号十进制或 base-prefixed `int64`、
  受限字符串、平衡括号与
  可重复前置 `not`，并允许上述数值算术、叶节点比较、零参数 tests 和短路 `and` / `or`。条件表达式作为
  statement 根时须以括号分组。优先级从低到高为条件表达式、`or`、`and`、`not`、比较、
  加减、乘/真除/整除/余数、左结合幂、test、一元符号、后缀取项/属性；数值零为 false，非零（含 NaN）为 true。
- `{% for item in expression %}`、可选 `{% else %}`、`{% endfor %}`；iterable 可为 borrowed
  sequence 或当前 evaluator 产出的 logical/conditional/list/tuple/dict，dict 按唯一 key 迭代。
- 循环体内 `{{ item }}` 指当前元素，`{{ item.field }}` 指当前元素字段。
- 普通循环提供只读 `loop.index0/index/revindex0/revindex/first/last/length/depth0/depth/previtem/nextitem`；
  nested loop 取最近一层，dict length 与邻项按首次插入顺序的唯一 key。首/末边界邻项为 Undefined，
  继续取其属性返回 `RENDER`。还支持 positional `loop.cycle(...)` 和 `loop.changed(...)`：前者
  eager 求值后按 `index0` 选择，后者在同一 loop 的调用点间共享上一组参数并隔离 nested loop。
  另支持位置参数 `range(stop)` / `range(start,stop[,step])` 全局函数，返回独立惰性范围值；
  支持 repr、正负索引、start/stop/step、真值/type tests、membership、按元素序列相等、循环与邻项。
  同名 context 绑定遮蔽默认内建；非 callable 绑定返回 RENDER。
  range 的单参数 count/index 方法与 membership 共用 O(1) 数值判定，支持属性调用与别名接收者；
  index 未找到返回 RENDER，位置超出 int64 返回 CAPACITY。
  调用复用 filter/test 的 keyword 语法与重复/顺序校验；range及其方法仅接受位置参数，
  dict/namespace支持keyword。通用CALL节点先求值目标，再从左到右求值参数，各一次，
  最后检查可调用性与绑定；短路不执行。内建与range绑定方法支持别名和集合存储，
  recursive loop已支持；宏、宿主callable与LoopContext方法别名仍待接入。
- 插值、statement 和 comment 的左右 delimiter 均支持 `-` 空白裁剪，例如
  `{{- path -}}`、`{%- if path -%}` 和 `{#- comment -#}`。

整数字面量的可选 `+` / `-` 可紧邻数字；带空白时作为一元算术。整数支持十进制以及大小写
`0b`/`0o`/`0x` 前缀，按 2/8/10/16 radix checked accumulation，统一输出十进制。值域限于 `int64`，
错误 digit/separator 返回 `SYNTAX`，越界返回 `JINJA_CMETA_ERR_CAPACITY`。整数和浮点 digit group 接受
Jinja 合法下划线分隔。浮点字面量由固定
C numeric locale 解析为 IEEE-754 double，`.5` / `1.` 返回 `SYNTAX`。算术接受 literal 及 CMeta
bool/有无符号整数/float，但无符号算术值必须不大于 `INT64_MAX`。纯整数 add/subtract/multiply/power
使用 checked arithmetic；`/` 恒产出 float，混合运算与负指数也产出 float；`//` 与 `%` 使用
Jinja/Python floor 符号，power 左结合。零除数、非数值 operand 及当前无法表示的 complex 结果返回
`RENDER`；string/list `+` 与 `~` 仍返回 `UNSUPPORTED`。浮点输出用固定 C locale 逐精度求得最短
round-trip significand，再按 Python/Jinja 的 `-4 <= exponent < 16` fixed/scientific 阈值重排；不修改
进程 locale。字符串字面量支持 `\\`、`\'`、`\"`、`\a`、`\b`、`\f`、
`\n`、`\r`、`\t` 和 `\v` simple escapes，以及固定宽度 `\xHH`、`\uHHHH`、
`\UHHHHHHHH`。固定宽度 escape 的 ASCII hex 位不足或非法、UTF-16 surrogate 和大于
`U+10FFFF` 的值返回 `SYNTAX`；未知ASCII转义保留反斜杠，八进制读取1–3位并编码为UTF-8。
反斜杠接非ASCII标量输出字面的 `\x`/`\u`/`\U` 十六进制文本，与上游ASCII backslashreplace步骤一致。
字符串 `not` 信息保留至解码后求真值，避免续行空串按编码长度误判。
相邻引号串由re2c合并为单token，parser span保留引号；两遍解码跳过段边界及间隔，形成单个STRING常量。
这不生成运行时CONCAT节点、不增加分配或依赖；转义以各段为边界，解码时间O(n)、额外空间O(1)，结果仍归compiled template所有。
实际CR/CRLF规范化为LF，反斜杠接实际换行输出零字节。
`\N{NAME}` 由共享 Unicode 17 名称/别名查询校验并解码；未知名称、空名称、命名序列返回
`SYNTAX`，raw/comment 不解析内部转义，转义反斜杠保留字面量。数据版本与大小写契约见
[Unicode 名称数据](../../unicode/data/README.md)，不是宿主 Python Unicode 版本的隐式副本。
re2c使用无符号字节避免将UTF-8高位字节误判为EOF。内容中的 `|` 不参与 filter 分隔；未闭合引号返回 `SYNTAX`。字符串 literal
比较先解码，再以显式长度按 canonical UTF-8 byte lexical order 求值；embedded NUL 参与比较，且不做
normalization 或 locale collation。同类型 literal 比较在 compile 折叠；含路径或异类型 literal 的比较
在 render 求值。路径值支持 CMeta bool、有/无符号整数、float 和 string；异类型/undefined equality 按 Jinja
返回 false（`!=` 返回 true），两个 undefined 相等；混合整数/float 不经有损整型转换即可精确比较，
NaN 对所有 ordering/equality 为 false 而 `!=` 为 true；异类型/undefined ordering 以及 enum、object、
sequence operand 返回 `RENDER`。比较可链式组合或作为括号 operand；链从左到右、每个 operand 只求值
一次，并在首个 false step 短路。单个 `=` 不是比较运算符，返回 `SYNTAX`。
字符串 membership 对 decoded UTF-8 bytes 使用 Salts `vstr_contains()`。sequence membership 只接纳
当前 scalar element 类型；非空 borrowed view 验证 data/stride/descriptor/element size/末元素 offset，
scan count 受 render `max_nodes` 限制。类型、metadata、容量错误分别返回 `RENDER`、`METADATA`、`CAPACITY`。
`and` / `or` 从左到右短路并返回被选 operand；未选择的路径或非法比较不求值。用于条件的逻辑根
只转换为一个 boolean provider node，因此即使被选值是多元素 sequence，分支 body 也只执行一次。
插值保留当前支持 scalar 的 operand 输出，undefined 输出空字符串。单个复合表达式最多 64 个后序
AST 节点，comparison step 也有 64 项固定上限；33 个以二元逻辑运算连接的 operand 因需要 65 个
节点而返回 `CAPACITY`。
条件表达式先且仅先求值 test，再只求值被选 branch；省略 `else` 且 test 为 false 时返回
undefined。连续 `if` 从左到右构造，`else` branch 递归接纳另一个条件表达式。branch 保留原
scalar/path/node 类型，未选 branch 不解析路径、不执行算术或比较。它与其他复合表达式共享
64-node 后序 AST 上限，render 只为最终值占用一个 provider node。
None 与 undefined 是不同的内部 value kind：None 输出 `None`、truthiness 为 false，equality 只与
None 成立。`is` 节点保存严格后序 operand index 和 test enum；render 对 operand 只求值一次，
再依据内部 value kind 或已验证的 CMeta descriptor kind 分类，最后应用 `is not`。类型测试不会
触发 generic 容器遍历；因此 CMeta map/sequence/set 的分类不代表当前 provider 已支持其 lookup。
带参数、点号和未注册 registry-dependent tests 保持 `UNSUPPORTED`，缺少 test 名或未分组连续 test 为
`SYNTAX`。
`[]` 对 base/key 各求值一次。已定义 base 的 missing、越界或不适用 key 生成默认 Undefined；
继续索引 Undefined 返回 `RENDER`。非空 sequence view 的 data/stride/descriptor/element size/末元素
offset 以及运行期 string 的完整 UTF-8 都在读取前验证，损坏返回 `METADATA`。`[]` 或分组表达式之后可继续
使用 `.identifier` 读取 CMeta struct field；attribute base 只求值一次，missing 返回 Undefined，继续读取
Undefined 返回 `RENDER`，identifier bytes 由 compiled template 独占。普通未分组 dotted path 继续折叠为
既有紧凑 PATH。list/tuple 按正负整数索引；dict 以当前 scalar 或递归 hashable tuple 为 key，
membership/iteration 只处理 key，重复 key 保留首次插入位置并采用最后 value。list/tuple 采用分类型结构
equality 与同类型 lexicographic ordering；dict equality 忽略插入顺序，dict ordering 返回 `RENDER`。
切片以 SLICE_LOOKUP 保存 receiver 和三项边界 tuple，按 receiver/start/stop/step 顺序求值。
`~` 使用独立 CONCAT 节点，Lemon 优先级位于加减和乘除之间；字符串直接复制、Undefined
转空文本，其余受支持值复用 repr。连接与切片结果共享同一 render-owned 字节工作区，
每个中间连接结果也计入累计预算；满额保持 CAPACITY，不被输出 callback 的 RENDER 覆盖。
同类型 list/tuple 的 `+` 将已求值元素浅复制到 VALUE workspace 的新区域；checked addition
和剩余容量检查先于复制。操作数快照保持不变，结果生命周期止于当前同步 render；
空结果不需要分配，错误停止 render 并走统一 cleanup，不增加公开接口或依赖。
list/tuple 重复同样复用 VALUE workspace：先验证 int64/bool 次数，以剩余容量除以输入
元素数检查乘法，再按结果长度线性复制不可变快照。非正次数／空输入不复制，结果保持原类型；
借用对象和所有中间结果均只在当前 render 生命周期内有效。
list/tuple 与 borrowed sequence 结果复用有界 VALUE workspace；range 保持惰性描述。
字符串按 Unicode scalar 扫描和复制，反转不改变 scalar 内部 UTF-8 字节；结果借用 render-owned
固定 byte workspace，累计 retained bytes 不超过 max_string_bytes，render 结束统一释放。
长度或 range 派生参数／中间运算超 int64 返回 CAPACITY；非法边界或零步长返回 RENDER。
单表达式 64-node 限制对应 list/tuple 最多 63 项、dict 最多 31 对。当前不支持 collection
comprehension/unpacking、generic CMeta map/sequence、Python 风格 item/attribute fallback 与 descriptor/method。
除点路径、已支持字面量、括号、受支持的一元 `not`、有界数值算术以及上述叶节点比较
之外的表达式、其他或串联 filter、除 `loop.cycle` / `loop.changed` 外的函数调用、
赋值、macro、include、extends 及其他 statement 在编译时返回 `JINJA_CMETA_ERR_UNSUPPORTED`，
不做静默降级。
控制块最大嵌套深度为 `JINJA_CMETA_MAX_BLOCK_DEPTH`；同时处于打开状态的 `if`/`elif`
分支总数上限为 `JINJA_CMETA_MAX_CONDITION_BRANCHES`。超过任一上限即返回
`JINJA_CMETA_ERR_CAPACITY`。
单个模板保存的表达式 AST 节点总数复用 `JINJA_CMETA_MAX_CONDITION_BRANCHES` 上限；
该计数同时覆盖条件与插值表达式，而不只是在同一时刻打开的分支。

完整 template source 先受 byte capacity 限制，再由 Salts
`vstr_utf8_invalid_offset()` 统一执行严格 UTF-8 准入；错误位置是第一个非法 byte 的
零基 offset。通过准入后，template re2c lexer 只承担 delimiter 的 byte scanning，不再
复制一套 UTF-8 decoder。原始文本和未转义字符串内容保持 UTF-8 bytes，不做
normalization、grapheme segmentation、case folding 或 locale collation。预组字符与等价的
combining sequence 因此保持不同表示；模板大小、source span 与输出容量也都按 bytes 计数。

当前 profile 的 identifier 固定为 `[A-Za-z_][A-Za-z0-9_]*`，语法及 trim whitespace
固定为 `SP/HT/LF/VT/FF/CR`。判定不依赖进程 C locale，避免 Windows code page 把 UTF-8
的单个高位 byte 误认成字母或空白。Unicode identifier 与 NBSP 等 Unicode whitespace
仍属于后续 profile，当前必须返回 `UNSUPPORTED`。

`elif` 被编译为有界嵌套 Mustache section。编译器在栈上保存当前打开块和分支的 borrowed
source view；它们仅在 `jinja_cmeta_compile()` 调用期间有效，不逃逸、不跨线程共享。容量
单位分别是控制块和条件分支，满额策略为立即拒绝；编译失败不返回部分模板。

`safe` 仅选择 Mustache 的 verbatim 输出回调，不执行净化或上下文验证。模板作者只能对
已经按目标输出上下文可信或净化的数据使用它，否则会把 HTML 注入风险暴露给调用方。

## 数据与状态归属

调用方拥有根对象、所有递归对象、字符串字节和 sequence 元素；整个 render 期间它们
必须保持不可变且地址稳定。`JinjaCMeta` 只借用这些数据，主事实源仍是调用方对象。

普通 struct 通过 `CMETA_DATA_STRUCT` 与 `cmeta_data_struct_shape` 查字段；bool、整数、
浮点、字符串和 enum 使用 CMeta 既有 descriptor/adapter。序列使用
`cmeta_data_collection_view`，其 `data/count/stride/element` 都由调用方提供；空序列允许
`data == NULL`，非空序列要求非空数据、非零 stride 和合法元素 descriptor。

render 内部一次性分配固定数量的 node wrapper。容量单位是 wrapper 个数，由
`JINJA_CMETA_RENDER_OPTIONS.max_nodes` 提供，零表示默认值。满额立即返回
`JINJA_CMETA_ERR_CAPACITY`；不扩容、不覆盖节点。wrapper 与字符串读取 view 都只在
单次同步 render 中有效，render 返回后全部失效。实现为单 owner、单线程实例；同一个
已编译模板只读且可被多个线程并发 render，每次 render 使用独立 workspace。
每个 wrapper 保存一个仅指向同次 workspace 中既有 node 的 parent link；路径首段按当前 node
到 parent chain 的顺序查找，选中后其余路径段只沿该 struct 解析。表达式路径解析本身不分配 node，
每次复合表达式求值只为最终值占用一个 wrapper；arithmetic/comparison/test/logical/conditional intermediate value 是同步调用栈上的
borrowed view，短路分支不分配、不解析，也没有 callback、挂起或跨线程生命周期。
positional call argument 的 compiled node index range 复用模板独占的 collection item table。
`range` 的 compiled call 保留目标节点用于上下文遮蔽检查；运行时嵌入 start/stop/step/count，
不物化元素数组，构造/索引/整数 membership/相等比较为 O(1)。距离与步长绝对值通过 unsigned
运算避免 INT64_MIN/跨零溢出；索引结果显式转换，不依赖超范围 unsigned-to-signed 行为。
wrapper 仍计入 max_nodes；巨大范围可直接 repr/索引，遍历前检查 provider 索引宽度与节点预算。
`loop.cycle` 会先求值全部参数再选择结果；`loop.changed` 将参数 value 浅快照保存在单次 render
独占的连续 arena 中，并由 iterable wrapper 持有 offset/count/initialized 状态。arena 以 `max_nodes`
为 value 上限，切换参数个数时申请新 range，满额返回 `CAPACITY`；所有借用随同步 render 返回失效。
collection literal 的 entry index array 与 dict key/value pair array 由 compiled template 独占；render value
按源码顺序构造不可变元素快照，dict 的 key/value 交替求值；真值、比较、索引、repr 与循环不再
重新执行元素表达式。独立 render workspace 首次需要非空集合时分配，以
`max_nodes * expression_count` 为 VALUE 数量硬上限，字节乘法先检查；不扩容、不回收仍被引用的
中间结果，随 render 一次释放。嵌套构造先预留父区间再填写子结果，合成标量先归一化，避免栈自引用。
list/tuple 取项与循环 child materialization 各占一个 wrapper；dict
用有界 O(n²) 扫描实现重复 key、唯一插入顺序和 order-independent equality，最大原始 entry 数为 31，
不引入无界哈希表或跨 render 缓存。
每个成功创建的 iteration child 在同次 workspace 中保存 `(index0,length)`，synthetic `loop` node
由 Jinja provider 的 name lookup 暴露；native evaluator 沿当前 parent chain 直接读取最近一层。
直接输出 metadata 的 synthetic object/scalar 均计入 `max_nodes`，表达式 intermediate metadata 仍只在栈上借用。
iteration child 还借用同次 workspace 中产生它的 iterable wrapper；该 pointer 在固定 node array 内地址稳定，
仅在同步 render 返回前有效。`previtem/nextitem` 通过同一 wrapper 的 checked index 路径创建邻项 node，
不会复制 CMeta payload；边界使用私有 Undefined sentinel，使直接输出为空、继续 lookup 失败。精确循环 alias
降低为 Jinja identifier 无法产生的私有名称，再解析到最近 iteration node，避免条件 section 的 boolean
结果替换当前循环元素。两元素 direct `nextitem` 路径的完整预算为 root、iterable、两个 iteration child、
两个 loop object、一个 neighbor 和一个 boundary Undefined，共 8 个 wrapper；容量 7 明确返回 `CAPACITY`。

## QueryVM 参考边界

表达式运行时参考 `Salts::QueryVM` 的边界设计，但不链接或复用其执行器。可复用的原则是：
compiled representation 由模板独占并在 render 期间只读；编译完成前验证节点引用和容量；
每次 render 使用独立 workspace；容量、内存、元数据和执行失败保持可区分诊断。

Jinja 仍使用模块私有的 AST/value evaluator。原因是 Jinja 的 undefined、词法作用域、短路、
safe-string 来源和类型转换属于模板语言语义，而 QueryVM 的 operand/backend 契约服务于
JSONPath、YPath 等查询执行。直接共用会让两套语义通过 callback 隐式耦合，也无法从当前
QueryVM backend 契约表达 Jinja provider 私有维护的 context parent chain。

AST 节点数受显式硬上限约束；compile 输入 span 只在编译期间借用，需要跨调用保存的数据
必须复制进 compiled template。布尔/整数 literal 比较在 Lemon 解析期常量折叠；同类型字符串
literal 比较由 Lemon 暂存两个 span 和 operator，compile 层复用现有 decoder 并以精确长度的临时
buffer 折叠。其余已接纳比较保存 typed operands 或严格后序 child index；链式比较另存模板独占的
有序 step array；collection 与 positional call 节点另存 element/argument index range，dict 保存
key/value node pair range。
finalization checked-count 并复制路径 bytes 与解码后字符串 bytes，render evaluator
只读该表示。运行期有/无符号整数比较不做减法或浮点转换，避免中间值溢出及精度丢失。
算术也保存严格后序 child index，re2c 以 operand/operator 状态区分 signed literal 与 binary sign。
render 将 bool、signed integer 和不大于 `INT64_MAX` 的 unsigned integer 保留为有界整数，float 保留为
double tagged value；纯整数 checked add/subtract/multiply/power 避免 C signed overflow，double
floor division/modulo 按 CPython 的 remainder/sign/rounding 步骤计算。
`INT64_MIN % -1` 返回 `0`，但对应 quotient 超出范围时返回 `CAPACITY`。
render 结果 node 属于单次 render workspace，不跨 render
或迭代复用。任一容量超限立即返回 `JINJA_CMETA_ERR_CAPACITY`，不得回退为近似 Mustache
语义。当前 AST 切片承载布尔、None、有符号 `int64`、double、受限字符串字面量、list/tuple/dict、零参数 identity/type tests、有界 numeric arithmetic、
deferred/nested/chained comparison、有界 logical tree 和 conditional tree；conditional
先求值 test 并只进入选中 branch。表达式 child index 在 compile 时验证为严格小于 parent，render 递归深度再由
compiled expression count 限制。
不参与比较的紧凑点路径条件和插值继续走原 lowering；item/group 后的 attribute 则由表达式 evaluator 求值。
表达式布尔值保留 Jinja 的 `True` / `False` 文本格式；
调用方 CMeta bool 的既有小写输出不变。整数使用私有 CMeta descriptor 暴露给现有 Mustache
provider，值本身由单次 render workspace 节点拥有。

字符串的事实源在 compile 成功后是 compiled template 独占的连续 decoded byte storage。builder 中的
encoded source view 只在 compile 内有效；finalization 先验证并计算 decoded length，再把 simple
与固定宽度 hex/Unicode escapes 解码到拥有型 storage，最终 AST view 被重定向到该 storage；
空字符串也指向其中的有效位置。scalar 合法性和编码宽度复用 Salts
`tstr_utf8_codepoint_size()`；因此 surrogate 和大于 `U+10FFFF` 的值不会进入输出。
deferred comparison 的路径也复制到同一 storage；所有路径与 decoded 字符串长度先 checked-add，
总量不得超过 template byte 上限。
render workspace 只拥有 `vstr` wrapper 并借用只读 payload，模板释放时统一释放 payload；
并发 render 只读共享，不产生跨 render 的可变状态。

renderer callback 的长度是 embedded NUL 的事实边界：streaming API 能保真传递 `\x00` 及其后续
bytes；string API 只返回无独立长度的 NUL-terminated buffer，不作为 binary-safe 接口。所有容量
仍按解码后的 UTF-8 bytes 计算，例如 `U+1F600` 占 4 bytes。

re2c 的 expression lexer 使用 NUL sentinel。由于 tag body 是原模板的 subview，parser 在
compile 调用内创建长度有界且 NUL 结尾的 scratch copy，再进行词法分析；AST 只保存解析后的
值，不借用该 copy。分配失败返回 `OUT_OF_MEMORY`，长度算术无法表示时返回 `CAPACITY`。

## 错误与失败状态

编译失败不产生模板。render 是流式输出，失败前已写出的字节不会回滚，与 Mustache
现有契约一致。错误区分非法参数、语法错误、不支持语法、容量耗尽、内存不足、元数据
错误和 renderer/provider 失败，并记录源偏移或 render 阶段摘要。

## 架构影响与验证

`jinja/` 独立拥有 `Salts::JinjaCMeta` target、源码、测试和 README，公开依赖
`Salts::Mustache`、`Salts::CMeta` 和 `Salts::Core`。依赖只从 Jinja 指向 Mustache；
`mustache/` 不引用 Jinja 文件或 target。现有 target 名、DLL/import library 名、C API、
状态码和运行时行为不变。public header 安装到 `include/jinja/jinja_cmeta.h`，target-based
consumer 继续使用 `#include <jinja_cmeta.h>`。

验证覆盖变量/点路径、`if`/`elif`/`else`、条件布尔/整数/浮点/字符串字面量、simple 与固定宽度
hex/Unicode escape 解码、非法 scalar 拒绝、embedded NUL streaming 和 non-BMP byte limit、
布尔/整数/字符串六种 literal 比较、path-to-literal/path-to-path 运行期比较、有/无符号边界、
loop 与 parent scope 查找、undefined/异类型 equality、ordering error、链式比较一次求值/短路/容量边界、
嵌套比较、string/sequence membership、数值算术 precedence/left-associative power/真除法/floor 符号/checked
overflow/零除数/负指数/混合 float/类型错误、最短 round-trip 与 numeric locale 独立性、NaN/整数边界比较、borrowed-view metadata 与 scan capacity、operand 所有权与 node budget、
`and` / `or` operand-return、短路、precedence、分组、重复 `not`、undefined、条件 sequence truthify、
逻辑树 64-node 边界与 source ownership、条件表达式的 value-return、省略 `else`、连续/嵌套
结合、短路、scope、statement 分组要求、64-node 边界与 source ownership、
escape 等价、Unicode scalar 顺序、embedded NUL、无隐式 normalization、比较与 `not`/括号的优先级、
输出 filter、
严格 UTF-8 拒绝 offset、2/3/4-byte scalar 与 combining sequence 保真、非 C locale 下的
ASCII identifier/whitespace 判定、三类 tag 的空白裁剪、sequence 循环与空分支、字符串和数值格式、非法 statement/filter、块不匹配、编译与
render 容量边界、C/C++ 公开头编译，以及现有 Mustache 回归测试。collection 另覆盖空值/嵌套/trailing comma/repr/truthiness、正负索引、membership、
list/tuple 结构及顺序比较、dict order-independent equality、tuple key、重复 key 最后值与唯一首次插入顺序、动态 CMeta value、循环 scope、source ownership、
provider node budget、31-pair/63-item AST 边界、malformed syntax、unhashable key 与非法 dict ordering；预期输出及错误分类由固定 Jinja 3.1.6 oracle 验证。
普通 loop metadata 另覆盖 borrowed sequence、collection/dict unique length、九个只读字段、native
arithmetic/logical condition、nested nearest-loop scope、unknown property Undefined 与 workspace exhaustion。
邻项测试另覆盖 borrowed struct 属性、list/tuple/dict 次序、边界 Undefined 与链式错误、native arithmetic、
nested nearest-loop 恢复、条件 section 内精确 alias 以及可复算的 8-wrapper 容量边界。

### 简单赋值与作用域

ASSIGN 指令分别保存模板拥有的目标名字节和 RHS 表达式索引。运行时先完成 RHS
求值与 VALUE 规范化，再写入当前层；不经过会丢失 Undefined/None 类型的输出适配器。
绑定仅归当前 render 所有，不修改 CMeta root 或 compiled template，也不依赖 Mustache。
if 不建层；for 每轮、空分支与 autoescape 建层并在退出时撤销局部绑定。
每轮显式写入普通循环别名，允许 set 遮蔽；隐式 loop 边界阻止外层同名变量遮蔽元数据。
编译时拒绝循环体/else 内赋值 loop 和以 loop 为 for 目标。已绑定 Undefined 的 range
不会恢复为内建函数。namespace 也遵循绑定遮蔽；裸 LoopContext 值仍未实现。

binding 槽沿用固定工作区分配模式，首次写入时分配，单独受 max_nodes 限制并检查乘法溢出。
同层覆盖不增加槽数；scope pop 只回收槽，不回收共享集合/迭代器 payload，统一由 render
cleanup 释放。名称查找为 O(B × 名称长度)，B ≤ max_nodes，容量不足立即 CAPACITY。
测试覆盖局部隔离、重复 render、共享迭代器、safe/Undefined 值、槽满和 RHS 失败。

### 元组解包

私有 assignment parser 分别返回目标树和 RHS 树；两个树各沿用64节点上限。
re2c 定位独立 ASSIGN token，Lemon 验证分组和元组；目标允许名称/元组及受限namespace属性，
括号内仍按表达式解析，因此 `(not)` 不等同于外层简化目标中的 `not` 名称。
编译器把目标树降为连续前序 UNPACK/ASSIGN 指令；UNPACK 保存直接子项数和子树末端，
只有根指令持有 RHS 索引，不把目标名称重新当作读取表达式求值。

render 先求 RHS，再递归校验每层数量并收集有界 pending 绑定，预检新增槽数后按目标
顺序发布。重复目标只消耗一个新槽，最后值获胜。形状错误不提交绑定；迭代器消费不回滚，
render 立即失败，流式输出沿用已输出字节不可回滚契约。工作区仅归本次同步 render。
现有有限序列先检查长度再复用 list 物化；items 只消费所需数量加一次耗尽探测，
不排空超长迭代器。临时快照计入累计 VALUE workspace，所有借用 payload 保留至 render 结束。
target遍历 O(T)，新槽预检 O(T×(B+T)×名称长度)，T≤64、B≤max_nodes；
物化成本沿用现有 Unicode/字典/序列规则，不引入新的模块依赖或公共 API。

### with 初始化与作用域

私有parse_with_binding以re2c token定位顶层赋值后的逗号，保留嵌套集合、调用及字符串
中的逗号/等号，复用Lemon与assignment目标验证。输出视图引用调用方输入，仅成功时
发布；编译器复制目标与表达式字节，不保存输入借用。with禁止属性目标与capture头。

WITH_BEGIN的end指向初始化目标子程序后的body；每个目标根持有其RHS索引，endwith
复用SCOPE_END。执行器先在外层求各RHS并按序解包到固定64叶pending数组，全部成功
才进入新scope，并复用与set共用的槽预检/发布函数。不能在每个初始化项后直接写入
provider bindings，否则后续RHS会错误地读取同头局部变量。

状态仅属于单次同步render；scope退出回收绑定槽，payload仍归现有render workspace。
新增绑定受max_nodes限制，执行深度沿用max_render_depth，错误终止render并统一cleanup。
初始化解包失败早于下一RHS，迭代器已消费状态及此前输出不回滚；无宿主写入。
扫描O(header bytes)，逐项复制/解析累计O(header bytes×项数)，项数受编译预算限制；
发布预检沿用O(绑定数²)。未增加公开接口、依赖或新的可增长存储。
with的loop参数可遮蔽循环对象；cycle/changed在eager求参后检查可见绑定，不绕过遮蔽
访问或修改真实循环。退出scope后恢复内建调用，短路分支不会触发调用。

### namespace 共享身份

NAMESPACE VALUE引用render-owned固定VALUE工作区中的稳定DICT句柄；变量、集合及循环
别名复制句柄，不复制对象身份。句柄是属性状态唯一事实源，每次写入先在剩余工作区
构造完整dict快照，再发布句柄的行指针与数量。scope退出不销毁对象，全部快照统一
随render cleanup释放，自引用无需引用计数。compiled template不保存可变对象状态。

ASSIGN_ATTRIBUTE保存owner长度、attribute偏移及目标子树末端。普通属性赋值在RHS
求值前检查全部namespace目标；解包校验后按目标顺序写入。capture在过滤与退出子scope
后写入父层对象。非namespace目标一律RENDER，不允许通过capture修改借用CMeta root；
这与固定上游3.1.6块捕获可写入普通dict的行为有意不同。
调用参数保存表达式索引与keyword view，keyword字节复制到模板拥有的字符串存储。

对象数与每对象字段数分别受max_nodes限制；句柄与快照共享
`max_nodes * template.expression_count`个VALUE槽。结果含n字段的写入消耗2*n槽，
对象句柄另占1槽；m个不同字段逐项构造消耗m*(m+1)+1槽，输入求值另计。
写入时间/空间O(n)，逐字段构造O(m²)，不宣称性能优化。所有容量计算先校验，
满额CAPACITY、分配失败OUT_OF_MEMORY，失败不发布当前快照，整个render终止并cleanup；
多目标中已写入的早先对象只存活至cleanup，不回写宿主，已发送外部输出不能回滚。
单线程render拥有全部状态；repr活动身份栈限64，重访输出`<Namespace {...}>`。

已覆盖原生dict/键值对/keyword构造、共享别名、集合循环、借用CMeta字符串key、
tuple/capture目标、容量、自引用及重复render。外部CMeta struct/map构造输入与
宏与宿主callable仍未实现；内建及range绑定方法别名已支持，不新增Mustache/QueryVM依赖或公开API。

### 通用CALL参数展开

编译后的collection item保留NONE/POSITIONAL/KEYWORD展开种类；关键词字节仍归compiled template。
运行时先求值目标，然后按位置/*与关键词/**两组归一化。每个源表达式仅求值一次，
*复用有界list物化，**复用native dict的首次键顺序及最终值查询，不将重复字典行误作重复实参。
字符串键通过统一scalar读取，支持借用CMeta字符串、Unicode与空键；非字符串键/非mapping为RENDER。
普通重名调用keyword拒绝；存在Python硬关键字标签时，依据
[固定Jinja signature实现](https://github.com/pallets/jinja/blob/3.1.6/src/jinja2/compiler.py#L481)
合并显式关键词与**（后者覆盖前者），软关键字不触发该规则。显式重复标签仍统一在编译时报SYNTAX。

单线程render拥有调用工作区；普通调用复用原有快照，展开调用追加归一化VALUE。
原始快照、展开值与嵌套调用共同受64槽上限约束，满额CAPACITY；返回或失败均恢复调用前游标。
局部关键词view只存活于调用期间，不跨工作区复用保存。输入iterator的消费不因后续失败回滚，
已输出字节也不回滚；render失败统一cleanup，字符串输出接口丢弃部分结果。
关键词冲突检查O(a²)，a受调用槽上限限制；dict唯一键与最终值查询复用既有有界O(n²)路径。
专用cycle/changed/recursive loop也使用同一归一化入口与LIFO wrapper；cycle/changed拒绝关键词，
recursive loop绑定唯一位置参数或iterable关键词。changed复制归一后的参数到既有changed_values，
不保存调用槽的指针；递归调用保留本层实参槽直到正文执行完成，内层与外层共同受64槽约束。
因此递归可能先触发调用槽预算，而非配置的render深度；都明确返回CAPACITY，不降级为额外分配。
TEST也通过该wrapper：先在独立槽位保存operand，再归一化实参，最后按num/seq或仅位置参数的
签名校验。删除原求值器TEST大栈帧及编译期invalid_test_arguments派生标记，避免两套绑定事实源。
普通test同样使用调用槽预算，nested test/call/filter共享64槽，失败统一回收，不新增备用存储。
FILTER同样先归一化，再由内建签名表绑定最多三个参数槽；普通/展开参数共享该事实源。
编译期不再保存default/boolean/count参数索引或invalid_filter_arguments；必填、未知名称、
位置/关键词冲突在实参求值后统一拒绝。filter block和capture过滤链复用同一路径。
没有新增公开接口、依赖或持久状态；稳定闭包cell与公开宏执行已接入，见后文。

### 块捕获

赋值头的re2c扫描区分ASSIGN与顶层PIPE/输入结束；Lemon接收私有捕获值占位token，
仅允许后续filter链，成功后转为CAPTURE表达式节点。该节点不从用户名称解析，
过滤器参数仍引用原始source span；不拼造模板文本或重新解析捕获结果。
CAPTURE_BEGIN跳过紧邻的目标子程序，进入子scope并路由输出到本层tstr；CAPTURE_END
封口buffer，在子scope求filter，再退出scope并调用现有目标赋值程序写父层。
与上游visit_AssignBlock/visit_Filter一致：autoescape开启时原始捕获和最终值均为Markup，
关闭时filter返回值保留原生类型，不强制字符串化。

每个执行过的capture拥有独立tstr，槽数组单独限max_nodes；所有capture累计payload单独
限max_string_bytes，先校验剩余容量再append，封口后buffer只读并保留至render cleanup。
此预算与表达式字符串池及最终输出预算分开，tstr额外容量和metadata仍有对应有界开销。
活动栈限64层，执行深度沿用max_render_depth。嵌套只切换当前槽，不复制父buffer；
VALUE引用已封口的稳定存储。所有正常/失败出口统一释放buffer，未完成capture不写外部sink。
TEXT与OUTPUT均保留provider首错，只有外部回调失败且provider无首错时返回RENDER。
遍历控制指令为O(指令数)，字节追加沿用tstr成本；不改变公开接口或模块依赖。

### filter block

FILTER_BEGIN/END复用capture_begin与提取的capture_evaluate；私有parser注入捕获operand
和PIPE token，filter名字节仍来自原输入，Lemon验证整个链，不拼造临时模板或变量名。
捕获求值在子scope内完成，随后filter_end检查字符串类型、弹出capture/scope并原样
写入父sink。与set不同：不把最终值强制safe/string，也不再次autoescape，非字符串
返回RENDER。此语义来自固定Jinja3.1.6的visit_FilterBlock/visit_Filter。

filter/set嵌套共用capture栈，所有封存buffer归render拥有直至cleanup。沿用捕获句柄、
累计字节及活动深度上限；失败不写未完成块，先前输出无法回滚。公开API及依赖不变。
测试覆盖局部参数、嵌套路由、安全标记、非法头/关闭标签、非字符串结果及预算边界。

### 宏与call：运行时接入约束与实现

此节是后续实现依据，不构成已支持声明。事实源为固定环境
`build/jinja-oracle-venv/Lib/site-packages/jinja2/parser.py`的parse_macro/parse_signature/
parse_call_block、compiler.py的macro_body/macro_def、runtime.py的Macro.__call__/_invoke，
以及oracle中18条macro用例。上游文档：
https://jinja.palletsprojects.com/en/3.1.x/templates/#macros 。

已验证的事实及架构影响：

- HIGH：宏按定义处词法环境读取自由变量，不是调用处scope。定义后同一外层变量更新
  可被后续调用看到；值快照会错误地冻结变量，直接使用当前binding栈则错误地动态寻址。
- HIGH：现有scope_leave复用binding槽，闭包不能保存裸binding指针。固定上游在with
  退出时清空局部cell，逃逸宏通过namespace调用时可输出内部`missing`；oracle已固定
  此实现边界。不能以“保留定义时值”代替它，也不能让复用槽被别的变量冒用。
- HIGH：固定3.1.6编译器在同一个函数内按词法深度与名字生成局部cell标识，两个顺序FOR
  的同名目标会复用`l_1_x`。逃逸宏在第二个FOR中可观察第二个目标值；循环退出会清空该cell。
  oracle的macro_runtime_sibling_loop_cell实测输出9。此复用必须由编译后的cell身份决定，
  不能依赖运行时binding数组恰好复用同一地址；不同名字/不同函数激活不能别名。
- MED：省略参数和显式Undefined不同。默认表达式在调用时、按参数顺序求值，可以引用
  前面的参数；不能在编译或宏定义时预求值，不能以Undefined充当missing参数标记。
- MED：varargs/kwargs/caller是否捕获取决于body对特殊名称的未声明访问；显式声明
  varargs并不自动接收多余参数。捕获kwargs时，位置参数已占用的同名keyword仍进入
  kwargs，而不是一律“重复参数错误”。该规则不能直接套用现有内建参数校验器。
- HIGH：宏结果安全性由调用时autoescape决定，宏body的编译逃逸语义还与定义位置有关。
  已验证定义在autoescape false、调用在true时`f('<')`返回`<`且is escaped为True。
  不能将整段宏body简单放到调用者当前autoescape状态执行。

候选与选择边界：直接内联宏文本无法处理别名/递归/caller，否决；复制当前provider
作为每次调用环境会复制错误的动态作用域且放大资源开销，否决。后续采用不可变宏
描述符、render-owned callable句柄、词法绑定cell及独立调用帧，复用原生VALUE和捕获
sink。必须先表达scope身份和cell失效/清空，再接入宏；不能为每个宏维护另一套全局map。

接口/状态：宏描述符归compiled template，含名字、参数、默认表达式与body指令区间、
自由变量引用和特殊参数标记；可变cell/callable句柄/调用帧仅归单次同步render。
callable别名复制身份，namespace/集合保存同一句柄。宏调用不读取调用者局部binding链；
输入参数、默认值、body写入与输出捕获按单一调用帧管理。宿主CMeta保持只读。
对象/cell累计数与活动调用深度必须硬限额，checked arithmetic，满额CAPACITY，
分配失败OUT_OF_MEMORY；语法错误SYNTAX，参数绑定/不可调用值RENDER。失败统一cleanup，
不回滚已消费iterator/此前外部输出；递归不能绕过max_render_depth。

迁移及验证：先以私有签名/自由变量分析测试固定描述符，再引入CALL VALUE分派并把
宏、别名、属性/下标取得的可调用值纳入同一入口，最后接入caller及展开参数。
既有range/namespace等必须尊重宏同名遮蔽，不能仅凭源码名称选择内建。
每步维持未接入语法明确UNSUPPORTED；回退时同时撤回相应可达编译入口与测试矩阵，
不并存兼容执行器。暂不增加公开registry或loader API；若后续需要公开接口变更，
另行说明契约并确认。最终验证需覆盖上述oracle、三profile、深度/容量/生命周期、
错误优先级与独立模块回归；当前18条上游预期通过不证明C宏运行时已实现。
重复形参在固定上游生成Python签名时抛出SyntaxError；oracle将其归类syntax并保留
原异常类型，其他未知异常仍直接抛出，不作为一般runtime错误吞掉。

私有签名前置现已实现：`jinja_expression_parse_macro_signature`接收不含macro关键词
的完整`name(parameters)`头，输出固定最多64参数的原输入span。re2c识别名称和
顶层分隔，Lemon验证各默认表达式，不执行表达式；默认span长度为零表示必填，
有效默认span可包含外围空白。支持现有Unicode标识符及名称位置的运算关键词，
禁止常量、属性/元组目标、重复参数、默认参数后必填与尾逗号。
扫描/默认验证O(输入字节)，重复名称比较O(P²×名称长度)，P≤64；临时NUL副本和
Lemon解析器在返回前释放，失败不发布签名，错误offset相对原输入。原生lowering已复制
名称与默认表达式到template存储；运行时cell和调用帧已接入，不能保存悬空view。

私有`jinja_template_describe_macro`已汇合MACRO/CALL的签名、默认引用、特殊参数能力、
正文节点半开区间及CALL接收表达式。所有非空span统一为完整模板字节偏移，空span为{0,0}；
CALL表达式和默认值保留解析器给出的外围空白，不重新裁剪或求值。描述符仅借用source/tree，
不持有分配所有权；lowering必须复制源字节或保证source寿命。成功才一次发布结果，
错误偏移相对完整source；CALL解析临时AST在返回前释放，容量沿用既有解析器限制。
宏帧分析已消费该描述符，discovery仍使用原头解析，避免引入不同的能力验证副作用。
该描述符是原生指令lowering的输入，不是已经可执行的宏或运行时闭包。

公开`jinja_cmeta_compile`统一消费owned模板树，将macro/call下放为FUNCTION指令、
函数表和形参表。FUNCTION.target选择函数，end跳过正文；body_begin/body_end是原生指令半开区间，
parent记录词法函数归属，CALL目标和默认值存表达式索引，缺省项用SIZE_MAX。名称指向template拥有的
program_strings偏移，表达式借用已由既有finalize统一复制。所有构建状态单次compile独占，
成功结果不可变；release统一回收函数/形参表，错误路径不发布半成品。复杂度沿用已有编译器，
额外函数定位O(functions×tree nodes)，受64函数及源字节给出的节点上限限制；累计形参及表达式各限64。
所有模板使用源字节约束下的整树。宏/call边界阻止循环控制指向定义者的外层循环。
公开compile/render已统一接入，私有compile_functions/render_functions阶段入口已删除。
以下词法cell与闭包执行复用同一原生产物，不保留旧运行时或兼容分支。

`jinja_cmeta_layout.c`现为私有原生编译生成只读词法帧、绑定与cell布局，复用已有scope分析。
每个cell按owner（引入函数激活的帧索引）、level及名称去重；同层相邻for同名槽共享，
宏参数/过滤辅助函数/递归循环的不同owner隔离。递归body与else同owner，父scope仍指向定义帧。
ALIAS的source_cell仅描述初始化来源，本地写入不会通过它写回祖先。被正文使用的loop在body
分析完成、子帧分析前补为ARGUMENT，使嵌套宏捕获正确的循环帧，而非外层同名变量。
作用域链与名称在单次compile内借用；最终名称复制到cell_strings，其他布局只留索引，release统一回收。
临时帧工作区按实际引入帧的语句预留，不扩容；cell与绑定各限4096，超过额度在分配值身份表前失败。
线性cell去重最坏O(cells²×名称字节数)，受4096硬限，未作性能收益声明；所有公开模板使用同一布局规则。
函数描述符scope连接其参数/body布局。每个owner现有连续slot与聚合cell_count。

私有 `jinja_cmeta_cells.c` 已提供稳定 activation/cell 存储，使用与provider完全相同的VALUE类型。
定义parent决定查找链，调用者不参与；bound区分missing与显式Undefined。clear仅清本地binding，
不清ALIAS源、不回收仍可被闭包引用的地址。store以显式activation/cell上限接纳，溢出和失败原子返回，
单线程销毁统一释放全部激活；模板及VALUE payload借用到销毁，store本身不可移动。
同一原生执行器现已接入cell读写、scope初始化/清理、函数闭包与参数绑定、默认值、递归和caller。
公开render可执行这些产物；macro/call、闭包及LoopContext均通过公开入口。
函数结束保留被内层闭包引用的激活；普通局部scope退出清值不回收地址。
递归循环跨迭代复用同一激活，body/else共享cell，递归调用使用定义parent并恢复现场。
caller作为synthetic keyword在显式keywords之后、**之前进入同一次参数收集/重复检测；
位置已绑定的同名keyword保留给kwargs，而不是无条件报重复。默认值先看到完整实参槽。
事实：公开宏接入后，原生测试116项、890断言通过；Clang Release、MSVC Release与MSVC ASan
的Jinja均6/6，主测试440项/2987断言通过；MSVC Release全仓45/45。
只读复核的默认参数上下文泄漏已修复；未做OOM注入、二进制布局比较或完整上游一致性验收。
宏属性name/arguments/catch_kwargs/catch_varargs/caller/explicit_caller复用函数描述符；
点号/字符串下标/attr共享读取，未知属性为Undefined。具名/匿名repr复用字符串表示和转义。
参数名借用模板，元组属于render且不移动；累计VALUE预算为
max_nodes × (expression_count + function_count)，加法/乘法均检查溢出。
函数描述符也能产生元组，因此不能只按复合表达式数预算；无函数模板预算不变。
本批10项测试覆盖Unicode、空参数、重绑定生命周期、容量和sink失败；ASan无报告。
私有VALUE_LOOP引用render-owned序列，loop_current只由实际迭代推进；读取属性/邻项复用原缓存。
NODE.loop_receiver与所属迭代loop_sequence分离，避免值作为集合元素或邻项时丢失接收者。
支持实时别名、length/repr、身份/类型、cycle/changed绑定与泛型递归调用；方法复用原参数展开。
宏与递归loop共享活动call_depth，另保留loop.depth语义上限；失败现场恢复时恰好递减一次。
本批17项测试，审查提出的接收者覆盖与根loop别名绕过深度限制均经红测复现并修复。
LoopContext自身迭代已接入first/list/for/成员判断/参数展开/解包/reverse；tuple保留同一接收者，
FOR_NEXT与别名消费共用sequence的物理游标及预读项，不维护别名游标。内部迭代器种类独立于表达式种类，
区分LOOP/ITEMS/REVERSE，避免loop|items绕过惰性mapping校验。
长度查询沿有界接收者链读取完整长度；缓存只控制实际消费与剩余容量，last仍按实际耗尽。
本次22项回归含嵌套lookahead、repr/revindex、耗尽、break/continue、配额与回调失败。
VALUE_MISSING保留清空循环参数闭包的原始singleton；普通名称与尚待默认值初始化的参数仍转Undefined。
默认参数检查归属当前函数及声明进度，macro/recursive loop函数体执行时隔离并恢复该上下文。
NODE.is_missing只负责render工作区值传递，不能将其当字符串或宿主CMeta对象；repr为missing且受字节预算限制。
原peek_missing与新增9项预读测试现已通过。sequence独占物理游标、previous/current/peek及首次长度快照；
预读missing推进源但不缓存、不推进逻辑index，普通yield仍保留missing；邻项视图不修改被保留节点。
已知长度保留源全长；无长度来源首次求长排除此前预读丢弃项，此后不因丢弃重算。
固定oracle共563条预期校验通过，不代表完整C运行时一致性。
公开全树编译与宏执行已统一接入；loader、继承和 registry snapshot 已接入，仍未覆盖的 upstream 范围继续明确拒绝。

全树节点现在由私有CSTL typed Vec独占；nodes/count仅为派生视图。源最多16 MiB，节点数不超过源字节数，
扩容前后只持有索引链接；发布后的树不再增长。调用方零初始化，成功reparse销毁旧树并转移新存储，
失败保持旧树及节点内容，destroy归零并可重复调用。源码span仍借用至分析/编译结束。
layout的node_scopes按实际节点分配，scope预留仅计创建帧的语句（FOR至多3，其余1，加root），
不再按所有TEXT/COMMENT节点预留大型scope；容量加法与分配乘法均检查。
大模板、宏前缀、900节点块链接、失败原子性及替换为空树回归已接入；parser112项/2131断言通过。
公开compile/render现已切换到owned全树和统一cell运行时。

签名还返回默认表达式的`default_references`：从已验证Lemon AST收集PATH以及简单
比较内联的PATH操作数，再由re2c读取路径根名称。属性名、关键字标签、filter/test名称、
字符串不计为读取；短路与条件分支内读取仍保留。名称去重，按最早源码出现位置排序，
span相对整个签名输入；参数名和caller/varargs/kwargs仅记录为读取，不在本阶段判定
自由变量或特殊参数能力。后续编译器必须结合参数声明、宏体局部声明及词法环境解析绑定。

`default_reference_parameters[i]`现为第i个默认引用对应的形参索引，或SIZE_MAX表示
仍待宏体/词法分析。必须等完整签名验证成功后，按全部参数声明匹配，不限于默认值
前面的参数。固定Jinja实测及oracle：`f(a=b,b=2)`无实参输出`|2`，传`b=3`输出
`3|3`，即使宿主`b=9`也不读取它；`f(a=a)`的缺省a为Undefined，不读取宿主a。
因此运行时必须先建立全部参数槽，再依次求缺省表达式；missing参数状态不能触发
外层变量查找。匹配仅比较已验证名称span，不求值、不分配额外堆存储，成本
O(默认引用数×参数数×名称长度)，上限分别128和64；最终结果仍一次发布。

该集合归调用者提供的签名结构，单线程解析在私有临时结果内累积，成功一次发布；失败
保持调用者结果不变。来源字节是唯一事实源，借用span在输入释放或修改时失效；不创建
可变运行时cell，不改变现有binding槽复用。每个签名最多128个不同默认引用，满额返回
CAPACITY并定位触发读取；解析/OOM沿既有错误返回。扫描与去重成本为O(AST节点数×
引用数×名称长度)，引用数上限128；没有额外无界容器或新模块依赖。

### 宏词法事实收集

私有`jinja_expression_collect_reads`与`jinja_expression_collect_targets`接受解析器成功
生成的AST及原输入，输出按首次源码位置去重的read/write名称span。读取包含短路分支、
比较内联操作数、调用目标/实参及捕获过滤器参数；属性、keyword标签与filter/test名称不计为绑定。
目标中普通名称是write，namespace owner是read，属性修改不是名称写入；空tuple没有事实。
赋值RHS传入解析返回的rhs view，目标传入完整赋值输入，两者不能混用偏移基准。

签名默认值复用同一读取扫描，再换算到完整签名并跨默认值合并；全部参数解析后的归属匹配不变。
两组各限MAX_REFERENCES，O(AST节点数×引用数×名称长度)，固定临时存储、成功一次发布；
span借用原输入，修改/释放后失效。非法计数/span返回INVALID，计数超限CAPACITY；无运行时分配、
公开API或依赖变化。契约要求已验证AST，不接受任意外部图。

这只是完整词法解析的输入，不是自由变量分析结果：参数/块作用域解析、稳定cell及宏调用帧仍待接入。
七项新增TinyTest验证这一私有边界，已有默认参数回归同时验证复用路径。

### 模板帧词法分析

续批已开放私有WITH/FILTER/CAPTURE/AUTOESCAPE子帧选择，不改变公开执行入口。
WITH目标按上游[parse_with](https://github.com/pallets/jinja/blob/3.1.6/src/jinja2/parser.py)
的param上下文投递ARGUMENT；重复目标只声明一次，运行时赋值顺序不属于此事实集。
父帧初值读取与子帧参数声明共享同一头部解析，防止两套偏移/分隔规则漂移。
FILTER在正文后分析filter参数；CAPTURE仅分析body，目标和尾部filter不参与RootVisitor，
此规则不表示尾部filter运行时依赖已解析。AUTOESCAPE在独立Scope内先读取策略再访问body。
事件/AST存储、容量和错误原子性沿用原入口，无新增分配或依赖。WITH参数去重按名称线性查找，
成本O(targets×distinct targets×名称字节)，受头部和事件限额约束。6项测试验证这些规则；
固定上游对照纠正了最初把WITH参数当普通store的MED设计错误，不用alias替代输入参数。
FOR续批通过`jinja_template_analyze_for_frame`显式选择BODY/TEST/ELSE，原入口FOR默认BODY。
BODY/TEST先声明去重目标参数，分别分析正文/条件；ELSE不声明目标。三者借用同一外层parent，
iterable归父帧，不把body帧当else的父级。缺省test仍声明参数，缺省else发布空帧。
复用单一有界walker，仅改变选择区间及入口事件，校验else范围/kind/parent后访问，不增加存储。
5项新增测试验证分支隔离、重复Unicode目标、嵌套边界、祖先引用与非法链接的失败原子性。
固定3.1.6 Symbols对照通过；模板解析器88项/964断言，三配置回归通过。
BLOCK续批已允许独立原始符号帧，强制parent=NULL（包括scoped）；非空parent立即INVALID，
不自动丢弃调用者传入的链。依据上游compiler.py:928的独立Frame，运行时context传递不等于词法父级。
只分析body，嵌套block跳过而分别分析；block内宏仍可引用该block符号。4项测试及固定上游原始
Symbols对照通过，模板解析器92项/991断言。原存储/预算/原子发布不变，无公开接口或依赖变化。
后续特殊名批次已新增`jinja_template_find_undeclared`：单名称查询选定body，返回源码read span；
穿透嵌套闭包但跳过BLOCK，按结构顺序处理target/value、body/filter及FOR末尾test。
NSRef、宏名和导入别名不作Name事件；未找到返回空span，错误保留旧结果。
根/BLOCK现按发现结果预声明self，BLOCK还预声明super；先行store遮蔽时不注入。
依据固定[UndeclaredNameVisitor](https://github.com/pallets/jinja/blob/3.1.6/src/jinja2/compiler.py)，
21项发现场景对照通过，模板解析器99项/1088断言。两次发现及绑定的工作区依次释放，不叠加峰值。
发现会检查穿透后的debug/i18n并返回UNSUPPORTED；返回表达式内文本首次read位置，不承诺AST首次位置。
LoopContext、自动子帧发现和闭包/宏执行已接入；self/super参数不代表继承渲染已实现。

宏特殊参数续批新增私有`jinja_template_analyze_macro_bindings`：正文发现决定caller/kwargs/varargs能力位，
隐式参数保留源码span；显式kwargs/varargs禁用对应extra接收，显式caller被发现时要求默认值。
默认值读取或call目标不会单独触发特殊参数。analyze_frame按显式参数→隐式caller/kwargs/varargs→默认值
生成事件，父帧同名变量不抑制隐式局部参数。非法caller返回INVALID及参数原始字节位置，失败不发布metadata/scope。
7项新增测试覆盖能力、遮蔽、call block、Unicode诊断及非法选择/超限/嵌套扩展错误原子性；
模板解析器106项/1173断言，固定上游6个宏属性及2个非法caller签名对照通过。
发现/签名/绑定工作区顺序分配释放，峰值沿用既有工作区；不新增公开API、依赖或运行时对象。
参数能力metadata不是调用执行，闭包cell、调用帧及默认参数求值仍待运行时接入。

私有`jinja_template_analyze_frame`消费已解析原模板树和同一源码，`SIZE_MAX`选择根，
macro/call opener选择参数/default/body；父scope先完成并冻结。参数先声明，set先读RHS，
目标的两组有序read/write事实按原位置归并，避免`ns,ns.attr`错误地先读取尚未声明的ns。
if内新写入发BRANCH_STORE；嵌套frame正文通过match跳过，父帧只分析其所属头部事实。
macro只存名称、for只读iterable、with只读初值、call只读调用表达式、filter只读参数、
capture只访问目标；block与autoescape整体隔离，import/from先读模板表达式再存别名。
规则依据固定[FrameSymbolVisitor](https://github.com/pallets/jinja/blob/3.1.6/src/jinja2/idtracking.py)，
不是模板源码的词法文本扫描；autoescape的上游Scope包装同样构成边界。

事件和AST临时工作区单次有界分配，最多65536事件；沿用128符号和64祖先限制，
错误直接传播并保留原结果，不求值、不改变运行时状态。结果名称span以完整源码为基准，
源码/parent链由单线程调用者保持不可变且地址稳定；工作区返回前统一释放。
遍历O(nodes + 各header解析 + scope分析)，不递归、不引入依赖/公开API或持久化格式。
本轮11项TinyTest及固定上游代表场景验证父级别名、分支初始化、隔离边界和目标顺序；
Release全量44/44、Clang Jinja含benchmark5/5、MSVC ASan Jinja4/4。
未支持的独立frame选择或debug/i18n语义明确UNSUPPORTED；不能把已生成父帧事件等同于
完整编译帧分析（尚缺LoopContext合成符号）、闭包cell或公开macro/call执行。

### 宏词法绑定核心

私有`jinja_expression_analyze_scope`按顺序消费已解析名称的参数、读取、写入及条件写入事件，
得到ARGUMENT/RESOLVE/ALIAS/UNDEFINED四类局部初始化来源。参数先声明；同scope先读后写保留
resolve，先写后读保留undefined；父级已有名称的读取直接沿父链解析，子级写入才建立本地alias。
新条件写入从父级alias或context resolve初始化，已有参数或已写局部的来源保持不变。
该规则依据固定[Jinja 3.1.6 Symbols](https://github.com/pallets/jinja/blob/3.1.6/src/jinja2/idtracking.py)，
不是“读取集合减写入集合”。

符号按首次本地定义分配固定槽，alias保存祖先距离与槽索引；父scope分析完成并冻结后才能分析子scope。
子结果借用父链及各自源码，调用者须保持地址稳定和生命周期；不得覆盖父或祖先作为输出。
单线程、无分配、失败不发布结果；事件上限65536、每scope128符号、含当前帧最多64层，满额CAPACITY。
事件/名称/顺序非法INVALID，错误offset相对事件所属输入。所有条件内写入均须标BRANCH_STORE；
该入口只计算初始化来源，不判断运行时分支可达性，也不生成语句事件。

模板根和macro/call帧事件投递及特殊参数分析见上节；LoopContext合成绑定、稳定cell和宏调用帧已用于公开macro/call。

### 宏调用前置：独立执行区间

表达式语句`do`也已接入同一指令执行器：re2c/Lemon解析现有表达式（含tuple），
编译为EVALUATE，求值后丢弃栈上VALUE，不创建用于输出的node、不格式化、不调用sink。
表达式产生的迭代器游标/loop.changed状态等仍由当前render拥有，预算与统一cleanup
不变，错误立即终止render。没有新作用域或额外堆分配；耗时/空间由原表达式决定。
当前C实现内置该标签；上游需[jinja2.ext.do](https://jinja.palletsprojects.com/en/stable/extensions/#expression-statement)，
oracle按用例显式启用。环境还支持按命名空间注册 filter、test、scalar global 与 custom tag；
custom tag 采用`{% tag(expr1, expr2) %}`表达式调用语法，回调结果求值后丢弃，失败立即终止 render。
这不是通用 Python 扩展加载器，也不开放宿主对象写权限。

MED／行为修正：do测试暴露借用CMeta紧凑路径的缺失中间段被当作末级Undefined。
公共路径解析现按是否还有剩余段区分：末级缺失继续返回Undefined，中间缺失返回RENDER；
这同时影响普通输出、过滤器、赋值和控制条件，不添加do专属处理或兼容fallback。

私有执行器现接受不可变指令区间`[begin,end)`及当前context；顶层render和filter块
使用同一入口。每次调用独占循环/autoescape栈，以入口scope/capture深度为返回不变量。
filter opener保存匹配closer索引，执行body区间后再求过滤表达式并写回外层sink；
不进入外层循环的回边，不重复转义，内层失败保留具体指令offset并终止整个render。
失败资源仍由render统一释放，不引入局部恢复或第二执行器。

区间执行本身无额外堆分配；C栈使用随活动filter嵌套深度增长，每层保留最多
MAX_BLOCK_DEPTH个循环/转义帧。现有词法深度、表达式节点、capture数量与字符串预算
共同限制嵌套；32层简单filter已覆盖，因为每层消耗2个编译表达式节点，达到64节点上限。
这不是宏调用帧的完成：内建callable VALUE与通用CALL入口已接入；稳定词法cell、
自由变量绑定及宏调用帧和动态递归预算仍须接入后才能开放macro/call。公开API和模块依赖不变。

### items 私有消费状态

native dict/Undefined的items返回一次性句柄，不转换成列表。源快照与游标位于render-owned
VALUE工作区；复制值共享游标。每个for拥有独立消费节点，正向段和else复用该节点，
不同for不重放对方缓存。普通路径iterable也通过compiled表达式进入该边界，计入64-node预算。
nextitem/last按需预取，length/revindex物化剩余项；list复用消费入口，first仅取一项。
缓存与tuple产物受原有VALUE容量限制，随render统一释放；不改变Mustache provider ABI。

Jinja输出回调检查provider首错：底层把NULL视为缺失值时，后续文本也不能继续写入调用方。
已输出内容保留，render_string失败仍返回NULL。上游生成器repr、
generic CMeta Mapping尚未覆盖，兼容矩阵不宣称完整items/Jinja支持。

循环别名字符串归编译模板所有，迭代节点沿消费时的parent链查找最近同名绑定；
集合创建上下文不充当消费作用域。显式成员路径只在首段查找变量，后续段不再查外层别名。
for的else不建立本循环的迭代绑定，但可访问外层绑定。测试覆盖嵌套遮蔽与恢复、
源码覆盖、空循环else，以及list/tuple/dict与items的嵌套消费。

生成器==/!=使用源句柄身份，不读取或消费源；dict key也使用同一身份相等规则。
in/not in调用既有next逐项比较tuple，匹配后停在该项之后，未匹配则耗尽；错误不转换为false。
普通PATH比较入口现先构造VALUE再进入统一比较器，保留iterator/collection类型，
借用sequence的成员判断也复用该比较器，避免在身份比较前强制做标量转换。
