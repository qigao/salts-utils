# Jinja2Cpp 测试场景映射

本映射把本地参考树 `build/Jinja2Cpp-master/test/` 用作功能清单，不把它当作 Jinja
规范。最终预期值必须由固定的 Pallets Jinja oracle 验证。

参考树采用 MPL-2.0。为避免把其 source-file copyleft 义务无意扩散到本项目测试，本项目
只提取功能分类和最小语义问题，并独立编写 TinyTest fixture、模板文本和断言；不复制其
测试文件、helper、长 fixture 或精确错误消息。若未来确需直接改编源码，必须把相应文件
明确隔离为 MPL-2.0、保留 notice 并记录来源版本。

| Jinja2Cpp 测试文件 | 可复用的场景分类 | 本项目阶段 | 权威判定 |
| --- | --- | --- | --- |
| `basic_tests.cpp` | 文本、注释、delimiter whitespace、字符串字面量 | M0/M1 | Jinja 3.1.6 oracle |
| `expressions_test.cpp` | 字面量、算术、比较、逻辑、条件表达式、precedence | M1/M2 | Jinja 3.1.6 oracle |
| `if_test.cpp` | if/elif/else 分支选择 | M1/M2 | Jinja 3.1.6 oracle |
| `forloop_test.cpp` | sequence、else、filter、loop metadata、递归循环 | M2 | Jinja 3.1.6 oracle |
| `filters_test.cpp` / `testers_test.cpp` | built-in filter/test、参数、链 | M3 | Jinja 3.1.6 oracle |
| `user_callable_test.cpp` | callback 参数、返回值和失败 | M3 | profile C ABI + oracle |
| `statements_tets.cpp` | set、with、filter block、raw | M4 | Jinja 3.1.6 oracle |
| `macro_test.cpp` | macro、default argument、call block、scope | M4 | Jinja 3.1.6 oracle |
| `includes_test.cpp` / `import_test.cpp` | loader、include/import、missing template | M4 | Jinja 3.1.6 oracle |
| `extends_test.cpp` | block、extends、super、scope | M4 | Jinja 3.1.6 oracle |
| `errors_test.cpp` | parse/render/loader 错误传播和 source location | 各阶段 | profile 错误分类 + oracle |
| `binding/*` | object/list/scalar lookup | CMeta adapter | CMeta contract + oracle |
| `perf_test.cpp` | plain/compile/render/loop workload 分类 | M0+ | 本项目 TinyTest benchmark |

当前已从 `expressions_test.cpp` 的逻辑表达式类别中提取“条件和插值中的括号组合边界”以及
“`not` 作用于括号表达式”、“两种 Jinja 大小写布尔字面量参与条件、插值及一元否定”、“有符号
十进制/二进制/八进制/十六进制 `int64` 的输出、组合、错误分类、边界和一元否定”、“引号字符串的
输出、真假值、所有权与 filter 分隔”以及
“Jinja2Cpp `LiteralWithEscapeCharacters` 所覆盖的 simple escape 解码”、固定 Jinja oracle
覆盖的 `\xHH` / `\uHHHH` / `\UHHHHHHHH` 解码与严格 scalar 边界、“布尔/整数 literal
的六种比较运算及 comparison-before-`not` 优先级”，以及“字符串 literal 的解码后六种比较、
Unicode scalar 顺序、embedded NUL 与不做 normalization”、“运行期 path-to-literal/path-to-path
scalar 比较、loop/parent scope、undefined/异类型 equality 与非法 ordering 错误”，以及“链式比较的
混合 operator、operand 单次求值、false-step 短路、括号嵌套比较与固定容量”语义问题，
以及 `testers_test.cpp` containment 参数表所代表的“字符串 substring、scalar sequence membership 与
`not in`”语义问题，
以及 arithmetic 参数表所代表的“一元符号、加减乘、true/floor division、modulo、Jinja 左结合 power、
float literal/混合数值 coercion、Python shortest-round-trip 输出、precedence、runtime CMeta operand 与错误边界”语义问题，
以及 Jinja2Cpp logical 参数表所覆盖的“`and` / `or` 组合与 `not` 交互”；后者进一步以官方
Jinja oracle 固定 operand-return、短路、`and` 高于 `or`、分组、undefined 和 scope 语义。
条件表达式类别进一步固定 value-return、省略 `else` 的 undefined、低于 `or` 的优先级、连续
`if`/递归 `else` 结合、未选 branch 短路、loop/parent scope、语句根分组要求、源码所有权与
64-node 容量边界。
`testers_test.cpp` 的类型/身份测试类别进一步被拆成零参数核心切片，并由固定 Jinja oracle
确定 `defined` / `undefined` / `none`、精确 bool、integer-vs-bool、float/number、string、
mapping、sequence、iterable、`is not`、一元/算术 precedence 及默认 Undefined 的 sequence/
iterable 行为。TinyTest 独立覆盖对应 CMeta scalar/struct/borrowed sequence、loop scope、源码
所有权、provider node budget、64-node AST 容量，以及合法未实现 test 与 malformed test 的错误分类。
参数化、点号、注册表和 callable tests 仍留在 M3。
`binding/*` 的 object/list/scalar lookup 场景已拆成后缀 `[]` 切片：固定 oracle 覆盖 struct、
sequence、负索引、Unicode string scalar、链式取项、Undefined 与语法边界；TinyTest 独立覆盖
CMeta string key、bool/整数 key、loop scope、source ownership、node capacity、非法 sequence/UTF-8
metadata。generic CMeta 容器与 item/attribute fallback 顺序仍留待后续工作。
`binding/*` 的 attribute chaining 场景也已拆出独立切片：oracle 覆盖 item/group 后缀属性、missing
attribute 与 Undefined base；TinyTest 另覆盖 loop scope、源码所有权、64-node 容量和 malformed syntax。
当前仅承诺 CMeta struct field，不承诺 Python descriptor/method 或 generic map 的 fallback 顺序。
`expressions_test.cpp` 与 `forloop_test.cpp` 的 collection 类别现已拆出 list/tuple/dict 切片：固定
Jinja 3.1.6 oracle 覆盖空值、嵌套、trailing comma、repr、truthiness、lookup、membership、
结构/顺序比较、类型 test、表达式 iterable 与循环；TinyTest 另覆盖 source ownership、provider
node budget、63-item/31-pair AST 容量、tuple key、重复 dict key 的最后值/首次顺序、动态 CMeta
scope、malformed syntax、unhashable key 与非法 dict ordering。comprehension、unpacking 与
generic CMeta map/sequence traversal 仍留待后续切片。
切片专项已覆盖 list/tuple、range、borrowed CMeta sequence、Unicode scalar 的正负步长、
省略/负边界、None、空结果、嵌套结果生命周期、非法语法、零步长和累计字节预算。
Oracle 记录非法常量浮点切片被上游折叠为 Undefined 的差异；本实现目前仍返回 RENDER，
因此 oracle 基线验证不能解读为所有记录均已由 C 引擎实现。
`forloop_test.cpp` 的普通 loop metadata 已拆出只读核心切片：oracle 与 TinyTest 覆盖 index/reverse
index、first/last、length、depth、borrowed/collection/dict 循环、native expression、nested nearest-loop
scope、unknown property 和 node budget；邻项切片继续覆盖 `previtem/nextitem` 的 struct 属性、
list/tuple/dict 次序、边界 Undefined、native expression、条件 section alias、nested nearest-loop 和
workspace 容量。Jinja2Cpp `LoopCycleLoop` / `LoopCycle2Loop` 场景现已提取为 positional
`loop.cycle` 切片，并扩展 Jinja 3.1.6 `loop.changed` 契约：oracle/TinyTest 覆盖 eager argument、
trailing comma、空 cycle 错误、scalar/multi-argument/collection equality、跨调用点共享、nested
loop 隔离及 render snapshot 容量。range 的位置参数、正负步长与循环已迁移并以 pinned Jinja 校验；
独立惰性值的 repr、索引、属性、相等性、membership 与 int64 极值另有 TinyTest。
count/index 方法另覆盖 computed/grouped/alias receiver、浮点与 bool 相等、未找到和结果容量错误。
Jinja2Cpp 的 range keyword 扩展不直接移植，Jinja 3.1.6 对其返回 TypeError。
recursive loop、其他 global/user callable 与 keyword argument
仍留待后续。
这些问题分别用本项目独立编写的 TinyTest 与固定
oracle case 覆盖；未复制上游 fixture、参数表或断言文本。

每个候选场景按以下顺序迁移：

Jinja 3.1新增的`items`另按固定Python oracle独立补充，不声称来自Jinja2Cpp测试。
TinyTest覆盖native dict/Undefined、唯一键与最终值、tuple产物、共享一次性游标、
truthy/iterable/sequence区分、延迟类型错误、first/list、for/else、loop邻居、容量、
重复render隔离和失败后的流式输出停止。嵌套循环内引用外层生成器的三个消费时序用例
现已迁入TinyTest；上游set用例仍仅在oracle中，不能算作C引擎已通过。
generic Mapping、生成器repr也仍是缺口。
身份相等比较、作为dict key、消费式in/not in现已覆盖；新增TinyTest检查匹配后的位置、
未匹配时耗尽、list/tuple不混同、借用sequence成员判断、非法源/有序比较错误和
if/else决策缓存及loop.changed的身份语义；对应固定oracle与C实现分别验证。

1. 只记录它要验证的 Jinja 语义，不复制原测试实现；
2. 写成最小、独立的 `test/oracle/cases.json` case；
3. 用固定 Jinja 3.1.6 Environment 验证输出或归一化错误；
4. Python Jinja 不接受的 Jinja2Cpp 扩展语法不得进入兼容 profile；
5. 先写会因缺少生产能力而失败的 TinyTest，再实现最小功能；
6. 在 feature matrix 中把对应项从“计划”更新为精确的“支持”或“有限支持”。
