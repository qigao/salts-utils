# Jinja CMeta 兼容性契约

本文档记录 `Salts::JinjaCMeta` 的实现范围。用户已取消旧兼容profile；当前直接向
Jinja 3.1语义推进，但不是完整Jinja实现。历史 legacy 标记不构成兼容承诺。

上游语义固定到：

- [Jinja 3.1.6](https://github.com/pallets/jinja/releases/tag/3.1.6)
- [Jinja template designer documentation](https://jinja.palletsprojects.com/en/3.1.x/templates/)
- [Jinja API documentation](https://jinja.palletsprojects.com/en/3.1.x/api/)
- [MarkupSafe 3.0.3](https://github.com/pallets/markupsafe/releases/tag/3.0.3)

Jinja 没有独立的 IETF 式 RFC。本项目把上述版本的官方文档、实现和由
`test/oracle/` 固定的 Python Environment 共同视作差异测试契约。

## Profile 状态

以下为逐批验证记录；当前状态以上方较新的批次为准，旧批次的未完成项不覆盖后续修复。

### 2026-09-11 环境与命名源码加载

事实：用户确认接口后，已实现opaque环境的create/destroy/compile/load、不可变词法配置快照、
同步load/release租约、NOT_FOUND/LOADER及拥有的UTF-8错误名称。成功加载的源码在编译后释放，
失败不发布模板；无缓存、重试、旧ABI兼容或busy/reentry状态机。模板借用环境，宿主负责销毁顺序。
ERROR布局从176增至448字节（当前x64），所有调用方必须重编译，不能混用旧benchmark或SDK。

事实：新增环境3项/31断言，C++接口3项/11断言；原公开699项/3817断言、native147项/1125断言。
最终MSVC Release、Clang Release、MSVC ASan的Jinja含benchmark各7/7，无新增编译告警。
限定只读复核未见HIGH/MED；4个直接DLL探针验证名称边界、UTF-8完整截断、嵌入NUL和空名。
首轮漏重建benchmark导致旧ABI越界，重建后通过；大spec的277处局部ERROR改用一个清零fixture，
消除实际复现的ASan递归测试栈耗尽，未增加用例/断言或改栈/递归限制。

未重跑全仓/完整oracle，未安装SDK或验证安装consumer。此批不等于include/import/from、
extends/super执行完成；后续若继续推进，M4/M5/M6保持开放，当前批次不覆盖 include/import/from/extends 之外边界。
[issue #26验证记录](https://github.com/qigao/salts-utils/issues/26#issuecomment-5626726914)。

### 2026-09-11 format / 字符串百分号

事实：format过滤器与字符串`%`共用私有实现，接入位置tuple、命名dict、参数展开及map；
支持flags、width/precision及星号、`%%`和d/i/u/o/x/X/e/E/f/F/g/G/c/r/s/a。
文本按Unicode标量计宽和截取；复用现有repr、Markup、tstr输出及C locale数值转换。
字段扫描加入既有re2c lexer，数值取模不变，无新公开API、依赖或fallback。

边界：沿用int64/uint64/double与累计字符串预算，不提供任意精度整数或Python任意对象协议；
repr继续沿用已记录的Unicode/对象表示范围。格式和参数错误返回RENDER，容量不足返回CAPACITY。
Markup格式按上游包装规则拒绝星号宽度及o/x/X/c转换，不将其静默改为普通字符串。

事实：3组紧凑TinyTest先红后绿；最终native147项/1125断言、公开699项/3817断言，
MSVC Release、Clang Release、MSVC ASan的Jinja各6/6，无新增编译告警。
29个定向上游模板一致。限定复核8个探针发现无字段格式误拒绝list/range的过严余参判定；
仅修正尾部判断，在原表补一行先红后绿，其余所查正常组合未见HIGH/MED。
未重跑全仓/完整oracle，未安装SDK；加载/继承和其余缺口仍开放。
[issue #26验证记录](https://github.com/qigao/salts-utils/issues/26#issuecomment-5622435746)。

### 2026-09-11 cycler / joiner

事实：接入两个全局构造器、cycler的current/items/pos及next/reset/__next__、
joiner的sep/used及调用。别名和绑定方法共享render内状态；保留原始值身份及Markup，
支持已有参数展开、属性读取、集合存储及构造器遮蔽。无新公开API或依赖。

MED / 事实：cycler.items正常输出暴露已失效的collection_context非空检查；
现有集合直接读取已求值元素，不再使用该上下文。删除私有字段、检查及无用途的转递参数，
不补造上下文或引入fallback；复用原功能表确认修复。

事实：本批共3组紧凑TinyTest，两个正常行为组先红后绿；native144项/1100断言，
公开699项/3817断言。最终MSVC Release、Clang Release、MSVC ASan的Jinja各6/6，
无新增编译告警；20个定向模板与固定上游输出或成功失败一致。
限定复核发现默认joiner sep身份错误：构造时固定静态默认值身份，现有短表补一行先红后绿；
其余7个正常宏/闭包/容器/绑定方法组合一致。
未重跑全仓/完整oracle，未安装SDK；实例/绑定方法地址repr、加载/继承及其余缺口仍未实现。
[issue #26验证记录](https://github.com/qigao/salts-utils/issues/26#issuecomment-5622101410)。

### 2026-09-11 wordwrap

事实：接入width/break_long_words/wrapstring/break_on_hyphens、map与参数展开；
按Unicode字符计宽、splitlines分段，保留内部空白和上游Markup分隔符语义。
默认换行符复制自编译配置；复用re2c和STRING_OUTPUT，不分配词块数组，
无新公开API、依赖、fallback或内部状态防御。

事实：3组紧凑TinyTest先红后绿，native141项/1080断言、公开699项/3817断言；
修后MSVC Release、Clang Release、MSVC ASan的Jinja各6/6，无新增编译告警。
28个定向上游模板一致，3字节恰限成功、2字节CAPACITY。限定复核发现0<width<1的
单字符步进差异，已局部修复并在既有表补回归，修后复核确认解决。
未重跑全仓或完整oracle；加载/继承、注册表及其余内建仍开放。
[issue #26验证记录](https://github.com/qigao/salts-utils/issues/26#issuecomment-5621800784)。

### 2026-09-10 xmlattr

事实：接入xmlattr及autospace位置/关键词/展开和map调用；native dict按现有items语义保持
键顺序与最终值。None/Undefined值跳过后才检查键；键必须是字符串，拒绝ASCII空白及/>=。
键和值复用escape/Markup转换，默认加前导空格，整个结果仅在autoescape开启时标为safe。
键应由应用固定或单独验证，不能把此过滤器当HTML净化器。
复用独占STRING_OUTPUT、字典迭代与统一清理，无新公开API、依赖、fallback或异常框架。

事实：两组12例短表先红后绿，native138项/1063断言、公开699项/3817断言；
MSVC Release、Clang Release、MSVC ASan的Jinja各6/6，无新增编译告警。
25个固定上游定向模板输出/成功失败一致，10字节恰限成功、9字节CAPACITY。
限定复核发现autospace真值提前计算会触发过滤loop预读；已改为属性转换后再计算，
一条回归固定namespace可见值顺序，修后复核确认解决。未重跑全仓或完整oracle。
排序类仍依赖尚缺的Unicode完整大小写映射；加载/继承公开接口仍待批准。
[issue #26验证记录](https://github.com/qigao/salts-utils/issues/26#issuecomment-5621492885)。

### 2026-09-10 wordcount / filesizeformat / urlencode

事实：接入三个过滤器及map调用。wordcount由既有re2c分类生成私有Unicode17 `(L|N|_)+`
词法扫描，不按空白简单切分，也不计组合标记为字母；未新增Unicode公开API。
filesizeformat支持十进制/二进制前缀、数值/文本输入与Jinja单复数规则，复用C locale。
urlencode支持路径、dict、二元组迭代及重复键，按UTF-8字节编码；路径保留斜杠，query空格为加号。
编码复用既有独占临时字符串和统一发布/清理，嵌套值转换不混入当前结果；无新依赖或fallback。

事实：3组12例TinyTest先红后绿，native136项/1045断言、公开699项/3817断言；
MSVC Release、Clang Release、MSVC ASan的Jinja各6/6，无新增编译告警。
另27个定向模板与固定上游输出/成功失败一致，4个恰限/超限字节探针通过。
限定复核发现并修复filesizeformat的1e24分档：整数比较阈值与浮点除法单位分离，
一条回归覆盖直接/map及相邻浮点输入；修后复核一致，无新增发现。
本批未重跑全仓或完整oracle。pprint仍未实现，现有repr不冒充排序/宽度换行的pretty printer。
[issue #26验证记录](https://github.com/qigao/salts-utils/issues/26#issuecomment-5621303551)。

### 2026-09-10 int / float / round

事实：接入int(default=0, base=10)、float(default=0.0)及round(precision=0, method='common')，
复用既有参数绑定与map调用路径。字符串转换支持Unicode Nd/White_Space、合法下划线、
进制前缀及小数/指数文本；转换失败保留过滤器default，Undefined仍报错。
round支持common的ties-to-even和ceil/floor，整数common保留整数类型；
复用固定C locale转换，不增加公开接口、依赖、内部恢复或异常框架。

事实：新增3组11例TinyTest先因UNSUPPORTED失败，接入后native133项/1021断言通过；
公开主套件699项/3817断言。三配置Jinja各6/6，无新增告警。
另30个定向上游对照中26个输出/成功失败一致；4个结果超出int64返回CAPACITY，
属于既有表示边界，不能宣称任意精度兼容。本批未重跑全仓或完整oracle。
限定只读复核另12个定向探针一致，未发现本批新增问题；
[issue #26验证记录](https://github.com/qigao/salts-utils/issues/26#issuecomment-5620989503)。

### 2026-09-10 map / select / reject 属性与过滤链

事实：接入map、select、reject、selectattr、rejectattr，延迟逐项消费，别名共享游标。
map支持已有过滤器调用或attribute投影（包括数字路径、逐层default）；select/reject支持
真值或已有test，属性版本仍输出原元素。位置/关键词/星号参数复用当前绑定，
参数构造时保存到render VALUE区，消费时不借用已回收的调用工作区。
batch与新过滤器共用私有输入游标，直接/延迟filter与test共用应用函数；无新公开API或依赖。
按用户要求删去绑定后重复的参数数量/槽位防御，不增加fallback或异常框架。

事实：3组紧凑TinyTest先因UNSUPPORTED失败，接入后native130项/999断言通过。
本批33个定向模板与固定Jinja3.1.6输出/成功失败对照一致；MSVC Release、Clang Release、
MSVC ASan的Jinja各6/6。此批未重跑全仓或完整oracle；加载/继承、注册表与其他内建仍开放。

### 2026-09-10 indent / truncate

事实：新增`indent(width=4, first=false, blank=false)`及
`truncate(length=255, killwords=false, end='...', leeway=5)`，支持现有位置/关键词/展开绑定。
indent支持字符串前缀、Python splitlines换行及Markup；truncate按Unicode标量计长，
非killwords按最后一个ASCII空格截断，保留无截断输入与安全后缀语义。
复用vstr、已有参数/字符串存储与Markup拼接；无新公开接口或依赖。
移除私有call编译中调用方已确定的kind白名单，不新增fallback或内部状态防御。

事实：新增3组TinyTest，11条功能模板已核对固定上游输出，先红后绿；native127项/969断言。
MSVC Release、Clang Release、MSVC ASan的Jinja各6/6，Release全仓45/45，无新增编译告警。
固定oracle805条预期及C无宿主/loader抽查653条重跑；23条既有差异、17条UNSUPPORTED、
135条排除均不变。loader/继承、其余内建与完整一致性仍未完成。

### 2026-09-10 单模板 block 与 self 执行

事实：`block`、`scoped`、未覆盖的`required`及前向`self.block()`已接入现有函数表、cell和执行器。
普通块使用模板上下文；scoped块保留可见局部值，包含loop和随后调用的宏/递归循环上下文。
无祖先时super为Undefined，执行未覆盖required块报RENDER；extends与模板加载已接入执行。
不新增公开API、依赖、fallback或违约调用状态机；块与宏/call共享64函数上限。

事实：现有native套件新增5组紧凑验收；修复scoped loop绑定及保留循环上下文的实际失败。
ASan已有递归回归暴露空参数表增加栈占用，改为共享只读常量后通过；不增加递归栈或放宽限额。
MSVC Release、Clang Release、MSVC ASan的Jinja各6/6，Release全仓45/45。
固定oracle805条预期通过；C无宿主/loader抽查653条仍有23条既有差异，17条UNSUPPORTED、
135条宿主/loader样例排除；不是完整一致性验收。常量求值、Unicode repr仍待统一校验。

### 2026-09-10 整数真除法舍入

HIGH / 事实：两个int64/bool做`/`时保留整数商/余数，最后一次舍入为double，
不再先转换输入而丢失低位。`9007199254740993 / 3`现为3002399751580331.0，
相关条件分支恢复正确；大分母、负数、INT64_MIN与halfway在同组回归中覆盖。
混合float、零除和其他运算不变；无新API、依赖、分配、状态机或fallback。

事实：仅新增一组表驱动公开回归，原错误先红；固定种子3064对int64按double位表示差分通过。
MSVC Release、Clang Release、MSVC ASan的Jinja各6/6，Release全仓45/45。
常量求值（含切片与Markup拼接）、Unicode repr、加载/继承和完整一致性仍未完成。

### 2026-09-10 Unicode repr设计与差异基线（尚未实现）

MED / 事实：公开DLL的容器repr仅转义ASCII控制字节。新增24条unicode_repr_*固定预期覆盖
控制/分隔/格式/私用/未分配字符、非BMP、可打印类别、引号、嵌套、Markup、宏重入与宿主字符串。
其中23条无宿主用例中20条复现错误输出，3条保护ASCII/可打印字符/普通字符串输出；另1条宿主用例
不在C端无上下文抽查范围。这是扩大覆盖后显露的既有缺口，不是生产代码回归。

事实：`jinja_oracle.py cases.json --verify`为805条预期通过；当前C端无宿主/loader抽查650条，
23条差异=原3条已知差异+本批20条Unicode repr；20条UNSUPPORTED、135条宿主/loader样例排除。
不验证精确错误类别。此批只改设计/参考测试，未修改Unicode公开接口或Jinja生产实现，未重跑C构建；
上批698项/3815断言与三配置绿测不证明本缺口已修复。

设计见[Unicode查询提案](../docs/architecture/unicode-re2c.md)与既有运行时计划。
建议在Unicode库新增`salts_unicode_is_printable(uint32_t scalar, int *result)`，公开API尚待用户批准。
独立查询保留原扫描器property mask与结构布局；分类复用固定re2c Unicode17数据，Jinja仅负责repr格式。
不改变普通字符串输出和HTML转义策略，不引入Mustache依赖或新运行时库。

计算：按[Unicode17 UnicodeData](https://www.unicode.org/Public/17.0.0/ucd/UnicodeData.txt)的
L/M/N/P/S类别加U+0020展开First/Last范围，未列出码点按Cn处理，排除2048个surrogate，
共1112064个合法标量，其中159613个可打印。与本地CPython3.12.7/Unicode15逐标量isprintable比较，
新增10615个可打印标量、移除0个，差异全是旧版未分配字符；本计算尚未测试未来C查询实现。
数据SHA-256为`2e1efc1dcb59c575eedf5ccae60f95229f706ee6d031835247d843c11d96470c`。
后续必须分别报告Unicode17全码点分类和固定Python15共同范围差分，不静默降级数据版本。

### 2026-09-10 重入执行诊断位置恢复

MED / 事实：macro、递归loop与filter统一经过私有execute_range包装，成功返回恢复调用方诊断游标，
真实失败保留最内层失败指令。宏成功后再发生的外层类型/预算错误不再误报宏内位置；
filter收尾仍定位endfilter，break/continue恢复及求值顺序不变。最终render_string转换sink错误为
CAPACITY/OOM时保留既有offset，NULL error仍有效。错误位置为原始源码指令级字节偏移，并非逐子表达式。
调用方游标只保存在同步C栈帧；不新增堆分配、公开API、Mustache关系或依赖，输出/清理契约不变。

事实：新增29项公开TinyTest，14项外层位置与2项最终sink位置先红后绿；主套件698项/3815断言，
native119项/905断言、模板解析113项/2139断言。覆盖嵌套/默认/caller宏、递归loop、过滤重入、
内层RENDER/METADATA/CAPACITY、流式前缀、UTF-8/CRLF、自定义定界符、NULL error及同模板再次render。
MSVC Release、Clang Release、MSVC ASan的Jinja各6/6；现有Release构建树全仓45/45。
独立只读复审逐项运行本批29项/103断言通过，未发现本批新增HIGH/MED/LOW代码问题。

固定oracle新增18条诊断模板后，781条上游预期校验通过；该oracle验证输出/错误，不验证本库字节偏移。
C端无宿主/loader抽查627条，仍有3条既有差异：常量浮点slice折叠、超int64字面量、do扩展开关。
另20条UNSUPPORTED、134条宿主/loader样例排除；该抽查不验证精确错误类别，不代表完整一致性。
未做OOM注入或安装/覆盖SDK。Unicode不可打印repr、性能复测、DAG优化、加载/继承、registry及完整审计仍开放。

### 2026-09-10 重入字符串隔离与存储边界

HIGH / 事实：string、concat及join的转换现各自拥有未发布tstr；完整转换成功后才复制进稳定slice。
过滤loop.length经由宏重入时，内层临时字节不再污染外层结果；内层逃逸view仍保留到render cleanup。
pending与retained逻辑字节使用同一减法准入，直接slice入口也计入pending；失败恰好释放本层暂存。
空串不分配；表达式求值/副作用顺序、Unicode/NUL、安全字符串、普通输出与autoescape保持既有语义。
宏capture与最终render_string仍各用原独立预算。无新增公开接口、Mustache关系或依赖。

HIGH / 事实＋计算：当前tstr/SDS在Windows LLP64使用32位长度，2^32会截为0。
新增私有纯准入对每个tstr限制`L=min(UINT32_MAX/2,SIZE_MAX/4)`，检查used<=L及incoming<=L-used，
给最多2倍增长及metadata/NUL保留余量。转换暂存、capture和最终字符串三个入口均检查，超限CAPACITY。
不因max_string_bytes配置大于L而统一拒绝小输出，不修改SDK；极值只测纯算术，不做巨量分配。

事实：本批27项公开回归（6项原始污染先红后绿），主套件669项/3712断言；新增3项内部长度测试，
其中2项先红，native119项/905断言。覆盖内层view、双层重入、容器/转义/Unicode/NUL、空串、
retained+pending恰限/超限、直接slice准入、内层错误offset、renderer失败与同模板再次render。
MSVC Release、Clang Release、MSVC ASan的Jinja各6/6；现有Release构建树全仓45/45。
固定oracle新增20条，共763条预期通过；C端无宿主/loader抽查609条仍有3条已记录差异：
常量浮点slice折叠、超int64字面量、do扩展开关。另20条UNSUPPORTED、134条宿主/loader样例排除；
该抽查不验证精确错误类别，不代表完整一致性。未做OOM注入或安装/覆盖SDK。

MED / 实测：相同Release负载、每轮10000次render、前后各3轮平均耗时的中位数如下。
测量命令为`ctest --preset win-release-user -R "^benchmark_jinja_cmeta$" -V --repeat until-fail:3`。

| 负载 | 改前µs/op | 改后µs/op | 变化 |
| --- | ---: | ---: | ---: |
| scalar转换（4输出字节） | 17.520 | 26.243 | +49.8% |
| 容器连接（10输出字节） | 27.347 | 43.881 | +60.5% |
| 16KiB连接（16393输出字节） | 93.075 | 97.977 | +5.3% |

计算为(after/before-1)*100%；后测scalar单轮17.994..27.267µs，波动明显。
新增独占暂存带来有界分配和O(bytes)复制，但未做同时间隔离A/B或分配profile，不能把全部差值归因于它；
也不能据此声称无性能回归。保留MED性能复测项，不回退已确认错误结果的共享区间实现。
仍开放：成功宏返回后的错误offset恢复、Unicode不可打印repr、DAG优化、加载/继承、registry及完整审计。

### 2026-09-10 容器循环 repr 与重入深度

MED / 事实：list/tuple/dict/namespace统一按活动祖先身份识别循环，分别输出
`[...]`、`(...)`、`{...}`和`<Namespace {...}>`；NODE转VALUE保留容器identity。
同级共享别名不省略，浅复制不按存储地址错误合并；后续独立输出不保留祖先状态。
私有同步栈frame借用当前VALUE，正常/错误都恢复父frame，不增加公开API、依赖或堆分配。
保留局部64层检查，并限制跨loop.length/过滤宏重入的实际活动容器链最多64帧，超限CAPACITY；
marker不压新帧。此前仅检查局部depth会放过66个namespace/132个frame的重入链，现已补测试修复。
max_value_depth/max_render_depth含义不变；流式失败保留已写前缀，render_string不交付部分结果。

事实：本批新增36项公开TinyTest，主套件642项/3630断言。25项初始正向中17项先红，
另有2项重入超限先红；覆盖循环/共享/复制、string/concat/join/capture/autoescape、
局部与跨重入恰限/超限、字节限额、marker写入失败、重渲染及后续借用错误。
MSVC Release、Clang Release、MSVC ASan的Jinja各6/6；Release全仓45/45。
限定独立复审的25条循环对照与7条活动链边界通过；未做OOM注入或安装/覆盖SDK。

固定oracle新增28条预期（27条循环/边界及1条新发现的错误结果），共743条上游预期通过。
C端无宿主/loader抽查589条，有4条差异：原3条常量浮点slice折叠、超int64字面量、do扩展开关，
以及新增记录的重入字符串污染。另20条UNSUPPORTED、134条宿主/loader样例排除，
该抽查不校验精确错误类别；oracle全绿不是C端全量一致性。

HIGH / 事实（未修复）：`repr_reentry_temporary_string_isolation`中，外层loop|string触发过滤宏，
宏中的123|string写入同一slice_bytes区域，外层把这段字节也算入结果。
最小复现：

```jinja
{%macro p(i)%}{%if i==2%}{%set ignored=123|string%}{%endif%}Y{%endmacro%}{%for i in [1,2] if p(i)%}{{loop|string}};{%endfor%}
```

预期`<LoopContext 1/2>;<LoopContext 2/2>;`，实际前部多出`123`；`~`和join也受影响。
本例仅repr LOOP/INTEGER，未进入新容器frame分支；属于独立字符串区间归属问题。
最小正确方向是隔离每次构建所拥有的输出片段并在成功后发布，不能回退共享游标覆盖仍被引用的内层字符串。
仍开放：此HIGH、Unicode不可打印字符repr、共享DAG优化、加载/继承、registry及完整语法/对象审计。

### 2026-09-10 比较与 tuple 键独立遍历预算

HIGH / 事实：运行时比较及字典键验证现共用单次 render 的访问计数与活动深度，
共享容器的重复比较和深链在满额时返回 CAPACITY，不再仅依赖保留节点配额。
MED / 事实：tuple 键验证已移除 expression_count 深度耦合，12层动态键及其无关表达式变体均成功。
根与叶均计一层；比较、容器叶子身份命中、tuple 递归键验证及 `is in(...)` 都进入统一边界。
访问总数跨表达式、循环和宏累计，退出检查帧仅恢复深度，新 render 才重置总数。
借用容器内容仍逐项校验，不使用身份快捷返回绕过 METADATA 或既有字节检查。

经用户批准，`JINJA_CMETA_RENDER_OPTIONS` 追加 `max_value_visits` / `max_value_depth`。
零默认值分别为1048576次访问和64层；深度1..64有效，超过64在输出前 INVALID_ARGUMENT。
调用方必须使用匹配头文件重新编译，不保留旧布局适配；本次未安装或覆盖SDK，安装消费验证仍开放。
`sameas`、编译期已折叠的比较及O(1)的range.count/index算术方法不属于运行时遍历；
短路未执行的比较不计费。这不是总指令/总输出配额或完整sandbox；共享DAG未做去重或线性化。
字符串叶子的字节成本仍沿既有来源边界约束：借用字符串受max_string_bytes限制，
源码字面量受模板/编译存储上限限制，而非一律受max_string_bytes限制。

事实：新增30项公开TinyTest及1项C++调用真实DLL的选项边界测试，主套件606项/3542断言。
动态键、共享DAG和深链先建立4项红测；审查发现 `is in(...)` 绕过根计数后，
另加3项红测并统一其比较入口。回归覆盖恰限/超限、63/64层tuple、独立/共享图、
错误后的再次render、短路、借用元数据、流式前缀及原始错误offset。
MSVC Release、Clang Release、MSVC ASan的Jinja各6/6；Release全仓45/45。
限定独立审查的21项C端诊断通过，未发现本批遗留HIGH/MED；未做分配失败注入。

固定oracle新增7条value_traversal_*后，715条上游预期校验通过。C端无宿主/loader抽查561条，
仍有3条已记录差异（常量浮点slice折叠、超int64字面量、do扩展开关）；另20条UNSUPPORTED、
134条宿主/loader样例不在该抽查范围内，且该抽查不验证精确错误分类。
仍开放：共享DAG重复访问优化、两类repr、加载/继承、registry及完整语法/对象模型审计。

### 2026-09-10 普通块头冒号公开执行

MED审查缺陷已修复：公开compile现在消费完整解析树的header末端，不再只为line statement去冒号。
if/elif/else、普通/过滤/递归for及for-else、autoescape、filter、块式set和宏/call中的合法冒号可执行。
空payload后残留的ASCII/Unicode空白复用既有裁剪函数处理；自定义delimiter、行前缀、raw/comment、
空白控制与原始错误offset保持不变。普通赋值、with、print、do及结束标签的非法冒号仍由parser拒绝。
不增加公开API、依赖、运行时状态或Mustache关系；源码有序游标合计O(nodes)、额外空间O(1)，
头部裁剪合计O(source bytes)，不复制源码。两遍扫描不一致走METADATA及统一cleanup。
依据：[Jinja 3.1.6 Parser.parse_statements](https://github.com/pallets/jinja/blob/3.1.6/src/jinja2/parser.py#L196)。

事实：新增18项公开TinyTest，16项先红后绿、2项保护错误拒绝/诊断优先级；主套件576项/3459断言。
新增22条block_colon_*固定预期后，oracle共708条校验通过。MSVC Release、Clang Release、MSVC ASan
的Jinja各6/6；MSVC Release全仓45/45。另抽查554条无宿主/loader样例，仍有3条已记录差异：
常量浮点slice折叠、超int64整数字面量、do默认内建与参考扩展开关；20条UNSUPPORTED及134条宿主/loader
样例未进入该C端抽查。oracle全绿不等同于C实现全量一致性。

限定只读审查未发现本批新增HIGH/MED；其额外对照中的upper未实现及raw:错误分类差异仍开放。
仍开放：比较工作预算、tuple键深度、两类repr、加载/继承及剩余语法/对象模型审计。

### 2026-09-10 NaN 容器身份语义修复

容器元素相等、list/tuple词典序的相等探测、dict键/值、成员查询和loop.changed现在使用
叶子身份或值相等；同一NaN可以命中自身元素/字典键，重复键保留首次位置及末次值，独立NaN不合并。
独立NaN==NaN和eq test仍为False。iterator成员查询仍消费到首次命中或耗尽，不重置别名游标。
原生list/tuple/dict即使身份相同也逐项比较，避免跳过内部借用字段的延迟METADATA/CAPACITY校验。
不新增公开API、存储、依赖或Mustache关系，错误和清理由原render边界负责。

事实：新增19项公开TinyTest；12项原始红测和2项审查回归先红后绿，另外5项保护相邻行为。
主套件558项/3406断言，固定oracle新增11条后686条预期通过；oracle计数不是全量C差分覆盖数。
最终代码：MSVC Release、Clang Release、MSVC ASan的Jinja各6/6，MSVC Release全仓CTest45/45。
只读审查复现过整容器快捷返回绕过借用校验的问题，修复后独立验证11条NaN C端输出与10条借用错误用例。
依据：[CPython RichCompareBool](https://github.com/python/cpython/blob/v3.12.7/Objects/object.c#L805)
及[Jinja loop.changed](https://github.com/pallets/jinja/blob/3.1.6/src/jinja2/runtime.py#L504)。

仍未解决：共享/独立容器比较的指数工作量、tuple键深度与表达式数耦合、Unicode不可打印字符repr和
容器循环引用repr。12层动态tuple键会RENDER而添加无关set后成功；此项与比较工作预算一起继续设计，
不能简单去掉旧深度检查放大共享tuple键的重复扫描。完整语法一致性仍未完成。

### 2026-09-10 重复 block 编译断言

公开 compile 在完整模板树解析之后、主体指令生成之前检查全部 BLOCK 名称。
同名定义在死分支、嵌套块、宏内部也返回 SYNTAX；offset 指向源码中最早的再次定义开标签。
名称按完整 UTF-8 字节精确比较，不按前缀、大小写或 Unicode 归一化合并；raw/comment 不参与。
私有 parse 仍保留这些声明；合法的唯一 block/extends 已接入执行并沿共享实例链处理，不再由公开路径返回 UNSUPPORTED。
依据：[固定 Jinja visit_Template](https://github.com/pallets/jinja/blob/3.1.6/src/jinja2/compiler.py)。

名称表只在本次 compile 内借用源码，CSTL Vec 和 stable_sort 各使用一份有界记录存储；
BLOCK 数复用 MAX_INSTRUCTIONS 上限，检查乘法溢出，超限/OOM 明确失败且不发布模板。
不改变公开 ABI、模块依赖或运行时状态；模板树为唯一声明事实源。

验证事实：新增11项公开 TinyTest 和1项私有解析回归；公开主套件539项/3358断言，
模板解析器113项/2139断言。MSVC Release、Clang Release、MSVC ASan 的 Jinja 各6/6，
MSVC Release 全仓 CTest45/45。固定 oracle 新增10条后675条预期校验通过；其中唯一block
用例只验证上游语义，不能算作C运行时支持。容量测试覆盖65536及65537个声明。
限定只读审查无遗留HIGH/MED；分配失败未做故障注入，OOM清理依照CSTL契约核对。

仍未完成整体一致性：前次审查的容器比较指数遍历、NaN容器身份语义、Unicode不可打印字符repr、
容器循环引用repr仍待修复；三配置绿测不代表这些已知缺口已关闭。

### 2026-09-10 sameas 有限实现及 replace 身份修复

`sameas(other)` 已公开接入，`'sameas' is test` 为True；不是值相等的别名，也不消费迭代器。
VALUE/NODE保存render局部序号或稳定来源/类型；None与bool按单例，int按固定CPython的-5至256单例范围处理。
宿主来源、宏元数据、容器元素、Undefined/None循环及batch发布路径已保存身份；赋值、实参及浅复制元素保留身份。
已覆盖现有tuple/plain string无变化操作、Markup新建、常量站点重执行及sum结果的新身份。

MED / 事实：审查复现replace未命中、相同参数及Markup零次数路径的身份错误；新增6项TinyTest，
其中5项先红后绿，1项保护已有Markup行为。普通字符串未命中、count=0、共享转换后参数可保留输入；
Markup结果或Markup转plain str始终获得新身份。等值但独立的宿主/Markup参数不视为相同参数。
运行时字符串常量来源复用仅用于replace判定，不全局合并literal-site身份或改变常量sameas结果。
依据：[CPython 3.12.7 replace](https://github.com/python/cpython/blob/v3.12.7/Objects/unicodeobject.c#L9632)。

事实：Clang Release、MSVC Release、MSVC ASan的Jinja各6/6；MSVC Release全仓45/45。
公开主套件528项/3281断言，sameas专属47项；固定oracle665条预期校验通过，非665条C端覆盖。
修复构建时发现user preset遗漏re2c stdlib路径，现已归入Windows profile；3个损坏PDB留存备份后重建。
未安装或覆盖SDK。更广泛的常量折叠/驻留（含bool/None转字符串）、跨TU宿主描述符及全部身份路径审计仍未完成；
不声称与CPython完整对象模型一致。loader/继承的完整 upstream 语义仍受当前 profile 边界约束，registry 已按 environment snapshot 接入。

### 2026-09-10 sameas 前置审计（历史记录，已由上节取代）

`sameas`仍未实现；不能用值相等代替对象身份。固定oracle新增13条sameas_*记录并完成631条预期校验，
覆盖单例、容器别名与浅复制、Undefined、range/绑定方法、namespace、迭代器、宏及参数错误。
这些是上游预期，不是C实现通过数。当前空容器、Undefined与普通值缺少完整实例身份，
需先落实创建/复制/取项的身份传播；方案与未完成步骤记录在运行时缺口计划的sameas小节。
本批不改变C运行时和公开接口，上一批481项/3187断言的构建证据没有作为本批重新运行的结果。

### 2026-09-10 内建能力查询

`name is filter` / `name is test` 查询本引擎实际支持的内建名称及 immutable environment snapshot
中的注册 filter/test，支持is not、动态/安全字符串、空参数展开，
test包含eq/equalto及`==`等符号别名。filter编译与查询共用名称映射；test查询复用解析名称表及runtime种类映射。
未知或尚未实现的名称返回False，例如`'upper' is filter`；`sameas`接入后其test查询已返回True。
registry 在创建 environment 时冻结为 snapshot，不支持 render 期间变更。额外参数/不可哈希操作数RENDER，其他非字符串值False；
名称长度限max_string_bytes，非法UTF-8返回METADATA，嵌入NUL按完整长度匹配，不截断。
未知filter/test的公开编译准入仍为UNSUPPORTED，不能用这些查询绕过未实现过滤器的编译拒绝。

测试：`test/test_jinja_cmeta.c` 的 `Jinja CMeta builtin queries` 共7项；4项新语法红测及UTF-8遗漏回归已转绿。
公开主套件481项/3187断言，固定oracle618条预期已验证；没有宣称全量Jinja一致性。
最终代码验证：Clang Release、MSVC Release、MSVC ASan的Jinja各6/6，MSVC Release全仓CTest45/45通过。

### 2026-09-10 slice过滤器增量

`slice(slices, fill_with=None)` 已接入独立列迭代器（与复合下标slice无关）。首次消费完整物化输入，
随后按列均分，前余数列各多一项；fill非None时其余列追加一项，即使整除也追加。空输入仍产生指定数量的空列。
创建不消费输入；负列数消费后产生零列，零/浮点/非法列数及无效输入在消费时报RENDER，参数绑定错误在调用时报错。
输入、列数及单列大小限max_nodes；保留VALUE累计计费，batch/slice嵌套消费共同限max_render_depth。
列状态为别名唯一游标，输入快照和已发布列活到render cleanup，填充值/元素保持既有共享身份；没有公开API或依赖变化。

测试：`test/test_jinja_cmeta.c` 的 `Jinja CMeta slice filter` 共15项，覆盖均分/整除填充/空列/Unicode、
惰性创建/别名全消费、负列数、参数展开/身份、batch混用及空列配额。审查复现并修复超大列数掩盖无效输入的错误时序。
公开主套件474项/3163断言，固定oracle612条真实预期通过；oracle总数不等于C完整差异覆盖。
最终代码三配置Jinja各6/6、MSVC Release全仓CTest45/45通过。
追加验收：负列数仍校验宿主非法UTF-8、动态宿主列数、嵌入NUL/多字节字符、sink满额停止及嵌套深度1/2。
尚无完整宿主元数据和所有参数组合的差异覆盖，不据此宣称完整Jinja支持。

### 2026-09-10 batch增量

`batch(linecount, fill_with=None)` 已接入独立惰性迭代器，支持位置/关键词参数、Unicode字符、
嵌套分组、共享消费游标及保留填充值身份。`first` 取组时已预读下一组首项，别名共享这一状态；
不先物化整个输入。零宽先生成空组，负宽生成整组；浮点等长可分组，但需要浮点次数填充时返回RENDER。
非法输入在消费时返回RENDER；空输入不因无效宽度提前失败。

状态及已发布行由单次render持有，cleanup统一释放；借用宿主数据仍要求render期间稳定。
状态数、每批输入项数与行大小受max_nodes限制，累计行槽计入既有VALUE预算，嵌套调用受max_render_depth限制；
超额返回CAPACITY，不静默截断。已发布行不复用；短输入按剩余项及有效填充宽度预留，非迭代输入计长只初始化一次。
公开接口和Mustache依赖关系未改变。

测试映射：`test/test_jinja_cmeta.c` 的 `Jinja CMeta batch` 分组，共19项，覆盖上述正向、错误及容量虚耗回归。
新增循环previtem/nextitem/last/length与共享别名预读、sink写入失败、填充容量8/9及嵌套深度1/2的边界验收；
流式失败保留已交付前缀，render_string失败丢弃部分结果，均通过公开接口验证。
公开主套件459项/3075断言通过；固定oracle596条预期已验证（不等于596条C实现一致性通过）。
最终代码验证：Clang Release、MSVC Release、MSVC ASan的Jinja各6/6；MSVC Release全仓CTest45/45。
上述测试不代表全量差异覆盖，不据此宣称完整Jinja支持；宿主元数据异常及所有参数组合仍需持续审计。

| Profile | 状态 | 用途 |
| --- | --- | --- |
| 当前实现 | 不完整、直接迁移 | 当前 `jinja_cmeta_compile()` / `jinja_cmeta_render*()` API |
| 旧legacy profile | 不保留 | 不提供兼容分支或选择器 |

按用户明确授权，默认普通插值不转义；escape/e 为表达式级显式转义。公开renderer为
JINJA_CMETA_RENDERER单write字节回调，不再暴露Mustache类型；调用方不得再次转义。
编译器直接生成 Jinja 原生指令，条件与循环由独立执行器处理；公开及私有链接均无
Mustache 或 QueryVM 依赖。安全字符串与 autoescape 块已接入独立求值器。

## 功能矩阵

整数词法现支持Unicode Nd数字：十进制ASCII非零首位之后的数字及十六进制数字部分，
例如`1٢`为12、`0x١f`为31；数字点下标使用同一规则。整数转换复用
`salts_unicode_decimal_value`，保留int64溢出检查、原始字节span和失败原子性。
二进制/八进制、纯零规则和十进制首位不放宽。Unicode浮点候选明确拒绝，不能被误读为
整数点下标；这是固定Jinja3.1.6的ast.literal_eval限制，而非只看float regex的接纳范围。
数字表固定Unicode17，较老Python字符数据库不认识的新数字按UCD单独测试，不声称版本一致。

私有Lemon语法支持filter/test结果继续调用，如`x|f()(1)`和`x is f()(1)`。
首组括号仍属于filter/test参数，shorthand参数中的连续调用仍绑定到该参数；
直接`x|f()[0]`拒绝，括号包裹的`(x|f())[0]`允许。结果调用复用有界CALL节点，
公开编译器支持一般CALL目标表达式；当前可执行值限内建及range绑定方法，其他非可调用值执行时报RENDER。

私有表达式/模板解析现于re2c词法阶段校验字符串的固定宽度转义拼写：
`\x`后2位、`\u`后4位、`\U`后8位ASCII hex，且八位转义不大于U+10FFFF。
词法允许上游可解析的surrogate转义，不能将其等同于UTF-8可输出字符。
非法嵌套或相邻字符串返回INVALID，
不发布部分树；错误偏移指向非法字符串token开头。模板/行语句扫描在非法引号token
处立即停止，不将其内部括号误计为结构。未知转义、转义反斜杠和八进制不因此拒绝，
raw/comment内容不进入该校验。
`\N{NAME}` 已使用共享 Unicode 17 名称/别名查询进行词法校验和公开字符串解码；
未知/空名称及命名序列拒绝，escaped slash保留字面量，输出仍受UTF-8字节预算约束。
大小写和数据版本契约见 [Unicode名称数据](../unicode/data/README.md)。

私有jinja_template_parse已建立默认分隔符下的完整模板结构树：源码顺序节点、
父块/分支、块首尾配对，复用表达式与头部解析器验证macro/call/for/if/block、
set捕获、with/filter/autoescape及模板引用；检查错误嵌套、重复else、缺失结束标签，
required块仅允许空白/注释。注释、raw及引号/括号内的伪分隔符不影响块栈。
树借用原始源码；公开编译器统一消费此树并生成原生指令，不执行加载操作。解析支持不等于相应运行时支持。
节点256、深度64、源码16MiB上限可通过私有编译宏调整，with绑定最多64；
超限CAPACITY，失败不发布部分树。
私有入口以extensions位集显式启用DO、LOOP_CONTROLS、DEBUG、CUSTOM：保存do表达式源码与
break/continue/debug节点，关闭的标签返回UNSUPPORTED，未知配置位返回INVALID。
do要求非空表达式且接受裸tuple；其余三种标签无参数。CUSTOM tag 另按公开 registry API 接入。
与上游解析阶段一致，循环外break/continue也可以形成语法节点；lowering仍需校验跳转作用域，
不能因此执行非法控制流。公开do能力不变；break/continue已内建接入，debug执行尚未接入。
I18N开关现已支持trans/pluralize/endtrans结构：单字符串context、最多64个显式变量绑定、
trimmed/notrimmed、单数/复数正文及单名称占位符。头部表达式复用Lemon，变量重名报错；
显式pluralize名称必须来自头部，隐式计数可来自头部或单数正文；重复pluralize、嵌套trans
和正文控制结构拒绝。源码保持不变，TRANS.name记录带引号context，translation_trim记录策略，
PLURALIZE.name记录显式计数名；公开环境通过translation回调执行消息查找和命名参数替换。
CUSTOM仅接受
expression-call statement，并在环境 snapshot 中按 tag namespace 查找 callback。
私有入口现支持变量/语句/注释的六个自定义UTF-8分隔符，每项非空且最多128字节，
起始符不得相同；非法配置INVALID、超限CAPACITY。共享re2c lexer消费同一配置表，
公开三参数compile通过COMPILE_OPTIONS接入同一配置，NULL使用默认值。根扫描优先完整raw开头，
再按起始符最长匹配；标签结束仅在表达式token边界识别，保留引号/括号屏蔽规则。
raw支持自重叠起始符与以控制字符开头的结束符。所有源码span仍是原始字节偏移。
私有与公开入口已支持line_statement_prefix和line_comment_prefix：缩进行首、同行Unicode空白、
括号/字符串续行、EOF结束及Unicode字符数前缀优先级；raw/tag内部不识别行前缀。
语句节点记录line_statement并保留原始字节span；注释不吞物理换行。
NULL view关闭行前缀，非NULL空view启用；空注释在换行/EOF不产生零进展token，
不复制固定上游在此边界重复输出空token的行为。
编译配置已支持trim_blocks/lstrip_blocks/keep_trailing_newline（仅0/1，默认0）：
TEXT/RAW.content从原始source/header推导有效字节范围，应用显式-/+、块后的单换行、
Unicode行首缩进及末尾单物理换行策略。raw内侧两个控制符独立保存，不跨标签裁剪文本。
右侧吞白先推进词法游标，末尾单换行先缩短词法视图，再推导content；避免把已经吞掉的
缩进识别成行语句，或把末尾已删除换行匹配成自定义结束符。完整源码仍由原始span保留。
原始source/header不改写；公开编译按newline_sequence（LF/CRLF/CR，默认LF）
归一化字面文本及字符串内的物理换行，再进行字符串转义解码；不变换显式转义及运行时字段。
NULL选项使用COMPILE_OPTIONS_INIT默认值，零填充选项非法；不保留旧二参数包装。

私有模板词法匹配自定义分隔符/行前缀时，将源码CRLF/CR视作LF（与固定上游tokeniter一致），
但不归一化配置字符串。统一matcher返回原始字节结束位置，普通tag、comment、raw共用；
不从CRLF中间开始匹配，source/header/content及错误偏移仍基于原始输入。
公开输出换行策略独立于词法匹配规则，配置字符串自身不归一化。

下标语法已支持数字点取项（`x.0`、`x.0.1`）、空下标`x[]`和复合下标`x[1,2]`、
`x[:,1]`、`x[::2,1:3]`，包括test简写参数中的后缀。空下标表示空tuple key，
复合项按源码顺序形成tuple；逗号后必须有项。单slice仍复用已有slice lookup，
复合slice作为独立SLICE值保存三个有界child，公开编译返回UNSUPPORTED，不伪装成tuple。
2026-09-10审计：固定Jinja3.1.6可折叠部分常量复合slice，但动态边界在生成Python阶段报SyntaxError；
`compound_slice_*`五条oracle记录区分该行为。完整动态执行方案待决，不把常量折叠成功当作上游完整支持。
数字点取项已复用现有runtime并通过链式/UTF-8字符串渲染测试；不接受带符号点下标。
数字点紧邻的浮点候选改按整数token匹配，模板结束符扫描也共享该边界。
整数词法现拒绝非零十进制前导零（01、0_1），允许全零及进制前缀后的单下划线
（0_0、0x_A、0b_1）；使用re2c同一规则，不保留旧拒绝行为。

表达式词法已支持Unicode XID名称（起始额外允许下划线，延续排除Join_Control）及
Unicode White_Space与U+001C–U+001F；复用re2c的Unicode17属性表，名称不规范化，
保留原始字节与偏移。相邻字符串间的Unicode空白已同步到解码器。
固定Python oracle使用Unicode15：新分配字符存在版本差异，未宣称全码点一致。
私有宏/循环/块头共享该词法；公开for已接入共享头部解析，支持Unicode名称与空白，
括号单名称及嵌套元组解包。公开语句入口统一按re2c首token匹配完整标签名，
if/elif/set/with/filter/autoescape/print/do/for的参数边界接受Unicode空白，
允许语法本身合法的无空格形式；print名/do名等不作为内建前缀执行。
不能由词法支持推导未实现的宏、加载及继承执行支持。

私有表达式解析已支持filter/test的点分注册名称，包括关键字拼写和Unicode名称；
filter块、set捕获及宏默认值复用同一规则。AST保存完整原始名称span（包括点号周围空白）
与test内建可用性，不以未注册为语法错误；这不表示提供了运行时注册API。
公开编译遇到未实现名称仍返回UNSUPPORTED，`defined.custom`不会按`defined`执行，
`safe.custom`也不会按`safe`执行。名称规范化与实际注册表绑定留给后续编译能力。

私有for头解析已保存名称/嵌套元组目标、iterable表达式、可选if过滤表达式及recursive标记。
iterable允许裸元组，条件表达式需分组；过滤条件允许条件表达式但拒绝裸元组。
目标拒绝属性赋值与顶层尾逗号；源span相对原头，各表达式树span相对对应源span。
失败不发布部分结果。公开编译先完整解析for头，再降低解包、过滤及递归循环；
非法头返回SYNTAX。括号不成为别名的一部分，
Unicode名称保留原字节、不做规范化；嵌套遮蔽及else复用现有循环运行时。
完整for/else/endfor结构已由上述私有模板树承接；公开编译已接入元组解包、过滤及直接loop(iterable)递归执行。

私有block/endblock头部解析已接纳lexical名称（包括true/none等字面量拼写），
校验可选scoped后required的固定顺序，以及可省略或精确匹配的结束名称。
名称span借用原头，错误偏移相对输入；私有模板树已接入块体与required限制，继承运行时未接入。

语法优先阶段：私有表达式解析器已接纳函数、filter、test 的 `*args` / `**kwargs`，
AST 保存展开类型与源表达式，校验重复展开及参数顺序。通用CALL已支持内建callable、
range绑定方法及其别名/计算目标、专用loop调用与已实现tests/filters的展开。
位置参数及*先于关键字表达式及**求值，目标与各源实参只求值一次。
**接受native dict的唯一字符串键（含Unicode、空键及借用CMeta字符串），重复字典键取最终值；
普通调用重名keyword返回RENDER。存在Python硬关键字参数时，按固定Jinja的字典合并规则
由**覆盖显式keyword；显式重复keyword仍统一为SYNTAX，不复刻上游重复标签的代码生成差异。
原始调用快照与展开实参共同占用64槽工作区，超限CAPACITY；容器物化另受既有render预算约束。
loop.cycle/changed仅接受位置参数（允许空**），cycle空实参为RENDER，changed按展开后的参数序列比较；
recursive loop接受唯一位置参数或iterable关键词，冲突/缺失/多余实参为RENDER。
递归调用的实参槽保留到该层退出，所有活跃层共享64槽；LoopContext别名仍未实现。
tests同样先保留operand快照，再展开参数并校验绑定：divisibleby接受num，in接受seq，
比较test只接受一个位置参数，其余已实现test不接受额外参数（允许空展开）。

私有 `call` 头解析已支持可选匿名caller签名、默认值、参数引用绑定，以及调用表达式。
宏与caller共享签名解析，调用表达式复用Lemon；非CALL根（含not/filter/条件表达式）拒绝。
签名span相对原头，调用AST的span相对返回call_span；失败不发布任何部分输出。
完整call/endcall及宏体结构由模板树承接，已接入公开宏运行时。
私有macro/call描述符已统一签名、默认引用、特殊参数能力及body/call区间，全部span相对完整模板，
失败不发布部分结果；宏frame分析与公开lowering已消费它，原3项公开宏验收已通过。

私有词法定位现返回最近定义的帧距离/符号槽，ALIAS不会追到初始化祖先；缺失名称独立表示。
定位与scope分析共用查找核心，校验完整父链深度及输入边界，失败不改变结果。
定位描述不可变编译期scope链；运行时通过下述稳定cell、闭包捕获及函数调用帧执行。

私有原生函数编译已接入macro/call描述符，生成template拥有的函数/形参表、默认表达式索引与正文
指令区间；源码释放后有效，跨函数循环控制拒绝。独立native TinyTest验证该阶段。
公开compile/render统一使用原生函数路径；已删除私有compile_functions/render_functions阶段入口。

私有原生产物已有只读词法cell布局，区分函数归属、词法层级、名称及ALIAS初始化来源；
覆盖循环loop参数、过滤辅助函数与递归body/else归属。新增私有稳定activation/cell存储，
按owner连续slot分配，定义链查找、scope清空不释放地址，bound区分missing/Undefined；
额度/错误原子性及生命周期由7项存储测试覆盖；native为116项通过、890断言通过。
公开render已连接cell求值、参数/默认值、递归、caller与闭包，并支持六个宏属性、
具名/匿名repr、点号/下标/attr读取、string/容器输出与转义组合。
私有LoopContext值已支持实时别名、属性/邻项、length、repr、类型/身份、cycle/changed绑定方法
与递归调用；集合元素/邻项保留原接收者。活动调用深度与宏共享，不能通过根loop别名绕过。
私有LoopContext自身可迭代，first/list/for/成员判断/参数展开/解包/reverse共享原循环消费位置；
产出二元tuple `(元素, 原LoopContext)`。嵌套length/revindex/repr使用源全长，last/nextitem按实际耗尽，
不因读取有长度来源而提前消费。last拒绝LoopContext；items创建仍惰性，消费非mapping时报RENDER。
元组及缓存归render所有且受既有节点/VALUE预算限制；容量或回调失败终止，不回滚已交付字节。
私有VALUE_MISSING区别于Undefined：循环参数闭包清空后可读取原始singleton，普通名称读取仍转Undefined；
宏默认参数按声明进度检查missing，递归loop与宏调用隔离默认求值上下文。表示、真值、类型、相等、
容器传播、兄弟循环重绑与输出预算已验证。nextitem/last遇missing只消费源项，不推进逻辑index；
前项取实际产出状态，重复预读继续消费，有长度来源保留全长，无长度来源缓存首次查询长度。
原peek_missing及新增9项组合回归通过，固定oracle共563条预期记录验证通过，不是完整C一致性覆盖。
三配置Jinja均6/6，公开主测试440项、2987断言通过；
MSVC Release全量45/45；公开宏已可执行，仍不等同于完整一致性验收。

私有表达式词法分析已区分名称读取、普通赋值写入及namespace owner读取；
去重保留首次源码span，默认参数复用同一读取路径。该事实集合已与块作用域
及闭包cell绑定；宏或caller经公开执行测试验证。

私有作用域事件分析现已区分参数、外部解析、祖先alias及局部undefined初始化，
处理先读后写、父级遮蔽和条件写入；父级先完成并冻结，alias使用祖先距离与符号槽。
私有模板帧分析已将根、macro/call的参数/default/body接到该事件分析，嵌套帧只贡献
父帧所属的头部读写，不展开其正文；目标内namespace读取与普通写入按源码顺序归并。
with/filter/capture/autoescape现可按opener独立分析：with目标为去重参数，初值仍归父帧；
filter先body后参数，capture按固定上游RootVisitor仅body，autoescape先策略后body。
FOR现支持BODY/TEST/ELSE独立选择：前两者声明去重目标参数，else不继承循环目标，
iterable仍由父帧读取；无test只保留参数，无else为空帧。续增5项测试后模板解析器88项/964断言。
BLOCK原始符号帧已支持且必须parent=NULL，含scoped块；嵌套block仍独立，宏可借用block帧。
后续已接入结构式特殊名发现：穿透嵌套闭包但跳过BLOCK，区分普通Name与NSRef/宏名/导入别名；
根/BLOCK按发现结果预声明self，BLOCK预声明super，先行store可遮蔽，不提供兼容分支。
新增7项后模板解析器99项/1088断言，21个发现场景与固定上游对照一致；三配置回归通过。
宏/call现已分析caller/kwargs/varargs能力并注入隐式参数，显式kwargs/varargs不接收extras，
正文使用的显式caller必须有默认值；默认值/调用目标本身不触发特殊参数能力。
新增7项后模板解析器106项/1173断言，固定上游6个宏属性及2个非法caller签名对照通过。
LoopContext合成符号、自动帧发现、闭包cell、宏调用帧及继承执行仍未完成，
debug帧内语义仍明确UNSUPPORTED。新增11项TinyTest（模板解析器77项/850断言）
覆盖作用域边界、Unicode原源码span、顺序及原子失败；不改变公开宏执行状态。

私有模板引用头解析已支持extends/include/import/from import的模板表达式、
ignore missing、with/without context和导入别名；include默认带context，import/from默认不带。
导入列表最多64项，拒绝导入下划线开头的名字；Lemon验证模板表达式，re2c负责后缀边界，
不把字符串、属性或test简写实参中的同名词误当后缀。该解析不加载模板，公开编译已接入 include/import/from/extends 运行时路径。

`print`语句已支持：逗号分隔表达式按顺序独立输出，无分隔符，逐项遵循safe/autoescape；
括号tuple仍作为单值输出，空print不输出，末尾裸逗号报语法错误。
Lemon仅在顶层裸tuple规则记录表达式列表标记，编译器复用OUTPUT指令；
不创建运行时tuple包装，但解析/编译仍计入该列表根节点和条目的现有容量预算。

`do`表达式语句已支持：复用现有表达式语法（含裸tuple），求值但不格式化/输出结果；
副作用、短路、错误和表达式资源预算保留。当前直接内置该标签，同时保留 environment registry 扩展入口；
上游须启用[jinja2.ext.do](https://jinja.palletsprojects.com/en/stable/extensions/#expression-statement)。
不因此承诺Python方法（如list.append）或未实现的通用callable。oracle用例可通过
`"extensions": ["jinja2.ext.do"]`显式启用此扩展；未指定时仍使用原空扩展环境，
其他扩展名或错误字段类型立即报错，不动态加载任意扩展。

缺失末级路径返回Undefined；继续访问缺失中间段（如`missing.name`、
`user.missing.name`）返回RENDER，不再静默返回Undefined，default过滤器不能吞掉该错误。

表达式补充：`and/or/in/if/else/is` 在操作数位置可作为名称，运算符位置含义不变；
`not` 仍为一元操作。路径插值支持单次和重复 `not`，偶数次也输出布尔值而非原值。
test简写参数支持 `in/if/not` 名称及现有后缀操作；`and/or/else` 终止该参数位置，
裸 `is` 保持连续test语法错误。无参test结果用于条件表达式时须分组。
简单名称、括号名称、元组解包及块捕获赋值已接入编译与运行时；namespace 支持范围见下表。
点号后的 `true/false/none`（含既有大小写形式）及 `not` 按属性名读取，与赋值目标一致；
不改变独立常量/一元not含义，属性后的算术、成员运算及test仍按表达式解析。

状态含义：`支持` 表示已纳入公开契约；`有限支持` 后必须写明边界；`不支持` 表示当前
编译器应明确返回 `JINJA_CMETA_ERR_UNSUPPORTED`；`计划` 只表示目标，不表示可用。

| 能力 | 当前实现 | 后续目标 |
| --- | --- | --- |
| 原始文本 | 支持：合法 UTF-8 字节保真，不做 Unicode normalization | 计划 |
| `{{ name }}` | 有限支持：Unicode XID名称组成的点路径、`true`/`false`/`True`/`False`、`none` / `None`、有符号十进制/二进制/八进制/十六进制 `int64`、十进制/指数 double 或含受支持 simple escapes 的引号字符串字面量；可加平衡括号；这些值可参与下述数值算术、比较、`is` tests、`and` / `or` 及条件表达式 | Unicode版本与绑定入口限制见正文 |
| 属性/元素 lookup | 有限支持：CMeta struct 紧凑点路径，以及 `[]`/分组表达式后的 CMeta struct 后缀属性；missing attribute 在已定义 base 上生成默认 Undefined，Undefined base 返回 `RENDER` | 计划：按 profile 定义优先级 |
| `[]` lookup | 有限支持：后缀取项可链式组合；字符串 key 读取 CMeta struct，bool/`int64` key 读取 borrowed sequence、list/tuple literal 与 UTF-8 string；dict literal 按 Jinja scalar/tuple key equality 查找且重复键取最后值；负索引从尾部计数，string 索引按 Unicode scalar 而非 byte；missing、越界或不适用的 key 在已定义 base 上生成默认 Undefined；Undefined base 返回 `RENDER`；generic CMeta map/sequence 与 Python 风格 item/attribute fallback 顺序尚不支持 | 计划：按 profile 定义 item/attribute fallback 顺序 |
| `[start:stop:step]` | 有限支持 list/tuple、borrowed sequence、range 与 Unicode scalar string；边界接受 int/bool/None、省略及负值，支持正负步长。零步长／非法边界返回 `RENDER`；长度或 range 派生参数及中间运算超 int64 返回 `CAPACITY`；字符串切片累计保留字节限 max_string_bytes。非法常量浮点切片尚未复现上游常量折叠为 Undefined 的行为 | 部分支持 |
| 注释 `{# ... #}` | 支持 | 计划 |
| delimiter 两侧 `-` 裁剪 | 支持 | 计划：以 oracle 为准 |
| `if` / `elif` / `else` | 有限支持：点路径、`true`/`false`/`True`/`False`、`none`/`None`、有符号十进制/二进制/八进制/十六进制 `int64`、double、受限字符串、平衡括号、可重复前置 `not`、下述数值算术/比较/`is` tests 及短路 `and` / `or`；条件表达式作为语句根时必须加括号 | 计划 |
| `for target in expression` / `else` | 有限支持：borrowed 连续 sequence、UTF-8字符串，以及当前 evaluator 可产出的 list/tuple/dict/range/items、logical、分组 conditional sequence；dict 迭代首次插入顺序的唯一 key。支持Unicode名称、括号名称、平面/嵌套/空元组目标；每轮复用赋值解包，形状全部校验后发布到本轮作用域，重复目标最后写入，禁止任何层次的loop目标；数量/类型错误RENDER，预算超限CAPACITY，空循环不解包 | 目标树限64节点，绑定与解包快照沿用既有有界工作区 |
| 字符串循环 | 普通、过滤及递归循环按Unicode标量顺序迭代，不合并组合字符或emoji字素簇；保留嵌入NUL，支持borrowed CMeta字符串。字符为普通字符串，不继承来源Markup的safe标记 | 复用list的线性解码与有界字符视图，进入正文前验证完整UTF-8；字符数限max_nodes，字节限max_string_bytes，视图计入既有VALUE预算；非法宿主UTF-8返回METADATA，超限CAPACITY，不输出本轮正文 |
| `for target in expression if test` | 支持现有表达式作为按需过滤条件，目标先解包；全部拒绝时执行else。index/first/last/length/revindex及邻居均以通过项为准，length消耗余项，last/nextitem仅探测所需项；缓存最终绑定重建的目标值，保留重复目标语义，不二次消费已解包迭代器。条件沿用循环外层词法作用域和转义模式，正文局部赋值不可见，namespace共享身份仍可见 | 过滤状态由单线程render持有且数量限max_nodes；缓存按来源上界预留并计入VALUE总预算，拒绝扫描也消耗节点预算；失败停止，不回滚流式已交付字节 |
| `for ... recursive` / `loop(iterable)` | 支持直接单参数调用及iterable关键字，复用目标解包、按需过滤、else；每次子调用使用循环声明处的词法绑定与转义模式，namespace身份共享。子输出先捕获为字符串，可赋值/过滤/再次输出；按声明处autoescape标记安全性 | 尚无LoopContext值与别名调用。递归层数限max_render_depth，共享scope/capture上限64；全部活跃表达式及调用参数快照各限64，捕获句柄限max_nodes，累计捕获字节限max_string_bytes；满额CAPACITY，失败不交付子调用未完成的捕获，但不回滚已交付输出或namespace修改 |
| `loop.*` | 有限支持：普通及递归循环的只读 `index0`、`index`、`revindex0`、`revindex`、`first`、`last`、`length`、`depth0`、`depth`、`previtem`、`nextitem`，以及 positional `cycle(...)` / `changed(...)`；`cycle` eager 求值并按 `index0` 选择，空参数返回 `RENDER`；`changed` 的参数 tuple 历史在同一 loop 的调用点间共享、nested loop 隔离，并受 render `max_nodes` 限制；首/末邻项为 Undefined；递归调用独立维护深度、索引及changed历史；LoopContext值、方法别名及共享自身迭代已支持 | 计划，具体字段另行列出 |
| 布尔字面量 | 支持 `true` / `false` / `True` / `False`；用于条件或插值，插值输出 `True` / `False`；`not` 可作用于当前支持的布尔、数值或字符串字面量；全大写 `TRUE` / `FALSE` 仍是普通标识符 | 计划 |
| None 字面量 | 支持：`none` / `None`；插值输出 `None`，truthiness 为 false，仅与 None 相等，与 undefined 不等 | 计划 |
| `sameas(other)` | 有限支持：None/bool/小整数单例、当前原生值及宿主借用来源的身份；支持别名、宏/loop/namespace/iterator、浅复制元素、other关键词和参数展开；不消费迭代器，错误参数RENDER。字符串及容器无变化操作的身份按已记录范围保留，Markup转换创建新身份 | 47项专属TinyTest及sameas_* oracle。完整CPython常量折叠/驻留（包括bool/None字符串转换）和跨TU宿主描述符审计未完成，不能等同完整Python对象身份模型 |
| 整数字面量 | 有限支持：`0`、正负十进制及大小写 `0b`/`0o`/`0x` 前缀的 `int64`，各进制接受 Jinja 合法下划线分隔；相邻符号属于 literal，带空白符号作为一元算术；用于条件或插值，零为 false、非零为 true；统一输出十进制；错误 digit/separator 返回 `SYNTAX`，越界返回 `CAPACITY`；Python 任意精度整数尚不支持 | 计划 |
| 浮点字面量 | 有限支持：Jinja 十进制小数/指数及合法下划线分隔，按 IEEE-754 double 解析；`.5` / `1.` 为 `SYNTAX`；输出使用固定 C numeric locale 的最短 round-trip 文本与 Jinja fixed/scientific 阈值；含 `inf`、`nan`、signed zero truthiness/输出 | 计划 |
| 字符串字面量 | 有限支持：单/双引号 UTF-8；支持 `\\`、`\'`、`\"`、`\a`、`\b`、`\f`、`\n`、`\r`、`\t`、`\v` 以及固定宽度 `\xHH`、`\uHHHH`、`\UHHHHHHHH`；hex 位不足/非 hex、surrogate 和大于 `U+10FFFF` 返回 `SYNTAX`；支持字面换行、CR/CRLF归一化、续行和1–3位八进制；未知转义保留反斜杠；支持 Unicode 17 `\N{NAME}` 单字符名称/别名；空串为 false；内容中的 `|` 不作为 filter 分隔；普通插值默认不转义 | 计划 |
| 比较 | 有限支持：点路径、`bool`、有/无符号整数、double、字符串及 collection literal 的比较；list/tuple 分类型执行结构 equality 与同类型 lexicographic ordering，dict equality 忽略插入顺序并采用重复键最后值，dict ordering 返回 `RENDER`；支持链式比较及括号 comparison operand；链从左到右、每个 operand 只求值一次，并在首个 false step 短路；混合整数/double 在边界处精确比较，NaN unordered；字符串按显式长度的 canonical UTF-8 bytes 比较；未定义/异类型的 `==` 为 false、`!=` 为 true；其余非法 ordering 在 render 返回 `RENDER` | 计划 |
| `and` / `or` | 有限支持：`and` 高于 `or`、低于 `not` 和比较；从左到右短路并返回被选 operand；支持当前 literal、点路径、比较、分组和 undefined；条件根最终只 truthify 一次；单表达式最多 64 个 AST 节点 | 计划 |
| 数值算术 | 有限支持：一元 `+` / `-`，二元 `+`、`-`、`*`、`/`、`//`、`%`、`**`；literal 与 CMeta bool/有无符号整数/double；Jinja precedence 与左结合 power；`/` 恒为 float，混合运算及负指数转 float，floor 除法/余数使用 Python 符号；纯整数保留 checked `int64`；溢出/超范围 unsigned 返回 `CAPACITY`，零除数/非数值 operand/complex 结果返回 `RENDER`；单表达式 64-node 上限 | 计划 |
| list/tuple/dict 字面量 | 有限支持：空值、嵌套、trailing comma、repr、truthiness、lookup、membership、比较、类型 test 与 `for`；tuple 单元素逗号及无括号 tuple；dict 保留首次插入顺序、重复键最后值，key 支持当前scalar、items句柄或递归hashable tuple，list/dict key 返回 `RENDER`；单表达式 64-node 上限使 list/tuple 最多 63 项、dict 最多 31 对；comprehension/unpacking 尚不支持 | 计划 |
| `~` | 有限支持：标量按 Jinja 文本、list/tuple/dict/range 按既有 repr 连接，Undefined 转空文本；高于加减、低于乘除；结果与切片共享 max_string_bytes 累计保留预算，超限 `CAPACITY`。autoescape 开启时安全片段使其他片段被转义，关闭时结果为普通字符串；CMeta object 到 Python repr 映射仍未完成 | 部分支持 |
| string `+` | 支持两个字符串相加，包括 CMeta string、Unicode 和连接后的切片；不隐式转换非字符串。与 `~`／切片共享累计字节预算 | 有限支持，保留 Markup 安全性 |
| list/tuple `+` | 支持同类型 native list/tuple 拼接，保留元素顺序与类型；不同类型返回 `RENDER`。结果快照计入既有有界 VALUE workspace，超限 `CAPACITY`；尚不支持 generic CMeta sequence 加法 | 有限支持 |
| list/tuple `*` | 支持 native list/tuple 与 int64/bool 双向重复；非正次数返回同类型空序列，浮点次数返回 `RENDER`；复制前按剩余 VALUE workspace 检查乘法容量，超限 `CAPACITY` | 有限支持，generic CMeta sequence 重复仍未实现 |
| string `*` | 支持 native/CMeta Unicode string 与 int64/bool 双向重复；非正次数返回空串，浮点次数返回 `RENDER`；结果计入与连接/切片共享的 max_string_bytes 累计预算，复制前超限检查 | 有限支持，保留 Markup 安全性 |
| complex、任意精度整数 | 不支持 | 计划 |
| 零参数 test 的 `()` | 支持所有已实现零参数 test 的空括号调用及 is not、空*与**展开；`(())`、`((),)` 等非空位置参数不被误判为空调用；非空位置/关键词参数求值后返回 RENDER | 操作数与展开实参共享64槽预算 |
| `is divisibleby` | 支持单个数值位置参数、num 关键词及简写参数、参数展开、尾逗号、is not；保留借用 unsigned 精度，避免最小 signed/-1 溢出；零除数、非数值、错误数量、未知关键词、位置/关键词冲突返回 RENDER | 显式重复关键词与关键词后普通位置参数为 SYNTAX，上游纯常量重复关键词折叠差异尚未实现 |
| test 简写参数 | 支持 atom 后的属性、索引、切片和已支持的 call 链，如 `2 is in range(3)`；算术、比较、逻辑运算仍在参数外结合。起始括号属于显式参数列表 | call目标受当前可调用值范围约束；通用call与test均可展开参数 |
| test 结果过滤器与交错链 | 支持 `3 is odd|string|length` 和 `3 is odd|string is string`；简写参数后的 filter 作用于 test 结果，参数内部 filter 需括号；沿用安全标记、autoescape 与短路。连续 test 要求前项有参数语法（含空括号）或显式分组，裸 `1 is integer is true` 返回 SYNTAX | parser-only 标记区分参数语法，沿用表达式节点上限 |
| `is odd` / `is even` | 支持 int64、借用有/无符号整数、bool 与 double；负奇数正确分类，非整数浮点两者均 false，NaN/Inf 两者均 false；非数值含 Undefined 返回 RENDER，元数据错误保持 METADATA | 与现有 is not、条件和表达式组合；不包含 Python 任意精度或自定义数值对象 |
| `in` / `not in` | 有限支持：exact decoded UTF-8 string substring；list/tuple literal 与 borrowed CMeta sequence 的当前 scalar equality；dict literal 检查 key；从左到右参与 comparison chain；borrowed sequence scan 受 `max_nodes` 限制 | 计划 |
| 条件表达式 | 有限支持：`value if condition else fallback` 与省略 `else` 形式；低于 `or`，condition 只求值一次且未选 branch 不求值；返回被选 operand，省略 `else` 的 false 路径返回 undefined；支持 Jinja 的连续 `if` 与递归 `else` 结合；单表达式 64-node 上限 | 计划 |
| `is` / `is not` tests | 有限支持：零参数 `defined`、`undefined`、`none`、`boolean`、`true`、`false`、`integer`、`float`、`number`、`string`、`mapping`、`sequence`、`iterable`、`escaped`；operand 只求值一次；`number` 含 bool 而 `integer` 不含；默认 undefined 是 sequence/iterable；单表达式 64-node 上限 | 计划 |
| 比较 tests | 支持 eq/equalto、ne、lt/lessthan、le、gt/greaterthan、ge，单个位置参数、is not、eager 求值；复用表达式比较器的数值精度、NaN、Unicode string、native list/tuple/dict/range 规则；关键词、错误数量及异类排序返回 RENDER | generic CMeta 对象比较能力不因此扩张；`is ==` 等符号名称按上游语法仍不接纳 |
| `is in` | 支持单位置参数或 seq 关键词及 is not；复用 in 成员查找，支持 string、native list/tuple/dict/range、borrowed sequence、items iterator；迭代器消费到首个匹配或耗尽。默认 Undefined 在成员运算符和 test 中均视为空 iterable | 沿用预算与元数据校验；None、不可迭代值、非法参数返回 RENDER，任意 custom CMeta 容器未实现 |
| `escape` / `e` / `safe` / `forceescape` | 支持表达式链及零参数调用；safe 标记字符串，escape/e 不重复转义安全值，forceescape 总是转义；结果为安全字符串，额外参数返回 RENDER | 复用有界字符串工作区；safe 不执行净化 |
| `length` / `count` | 支持无括号或空括号零参数过滤器，用于插值/条件/算术组合；Unicode scalar string、list/tuple、dict唯一键、range、borrowed sequence及Undefined。无长度值／额外参数返回RENDER，长度超int64返回CAPACITY | 有限支持 |
| `default` / `d` | 支持零至两个位置参数及 default_value/boolean 关键词；缺省默认值为空串，boolean按truthiness决定是否替换false值；返回原类型，参数按源码顺序eager求值；未知关键词、位置/关键词冲突、数量错误返回RENDER；重复关键词及关键词后的位置参数返回SYNTAX | 有限支持 |
| `first` / `last` | 支持零参数、可选空括号；native list/tuple、Unicode scalar string、range、borrowed sequence，以及dict唯一键插入顺序。first可消费items的一项；last拒绝items。空值/默认Undefined返回Undefined，不可迭代值及额外参数返回RENDER | 有限支持，generic CMeta容器及其他迭代器未实现 |
| `abs` | 支持零参数数值绝对值，bool转int，float保留float；非数值返回RENDER，int64最小值绝对值超范围返回CAPACITY，不声称Python任意精度兼容 | 有限支持 |
| `int` / `float` | 支持数值/Unicode十进制文本转换、default参数；int支持base=0或2..36、前缀、合法下划线及小数文本截断；float支持小数/指数、inf/nan及signed zero。非转换对象使用default，Undefined为RENDER | 结果受int64/double限制，整数超限CAPACITY；[数值转换验收](test/test_jinja_native_functions.c) |
| `round` | 支持precision及common/ceil/floor、位置/关键词/展开和map调用；common按ties-to-even保留整数/浮点类型，precision=None返回整数；负precision支持十/百位等舍入 | int64/double范围，超界或不可舍入输入明确失败；[舍入验收](test/test_jinja_native_functions.c) |
| `string` | 支持当前标量与native容器/range文本转换；Undefined为空串，结果可继续表达式操作；已有字符串验证后借用，其他结果计入累计字节预算；普通 string 转换不额外转义 | 保留 Markup；CMeta对象repr映射仍未实现 |
| `wordcount` | 支持当前值的字符串转换后Unicode `(L|N|_)+` 连续段计数，包括字母、各类数字与下划线，组合标记/符号分隔词；Undefined为0 | Unicode17分类，Python15尚未分配的新字符可能不同；[词数验收](test/test_jinja_native_functions.c) |
| `wordwrap` | 支持width、break_long_words、wrapstring、break_on_hyphens及map/展开；按Unicode字符计宽、splitlines分段、词与连字符断行，保留内部空白；默认分隔符取编译配置newline_sequence，Markup分隔符按上游语义转义并标记结果 | 输入必须为字符串；Unicode17分类及既有字符串额度；[换行验收](test/test_jinja_native_functions.c) |
| `filesizeformat` | 支持数值/文本转换、binary关键词及位置参数；默认十进制kB至YB，binary为KiB至YiB，按上游一位小数及Byte/Bytes规则；错误输入返回RENDER | double表示及已有字符串预算；[文件大小验收](test/test_jinja_native_functions.c) |
| `urlencode` | 支持字符串路径、native dict和二元组iterable；Unicode按UTF-8字节编码，路径保留`/`，query中`/`编码、空格为`+`，保留重复键/迭代顺序，返回普通字符串供autoescape处理 | 复用当前容器与值转换范围；generic CMeta mapping仍未实现；[URL编码验收](test/test_jinja_native_functions.c) |
| `xmlattr` | 支持native dict、autospace位置/关键词/展开与map；跳过None/Undefined，键拒绝ASCII空白及/>=，按escape/Markup语义生成属性；全部值转换后才判断autospace，结果仅在autoescape时safe | generic CMeta mapping仍未实现；键须由应用固定或独立验证；[属性输出验收](test/test_jinja_native_functions.c) |
| `list` | 支持native list/tuple、range、Unicode scalar string、dict唯一键、borrowed sequence、items迭代器及Undefined转列表；物化项数限max_nodes并计入VALUE workspace，超限CAPACITY，非迭代输入RENDER；字符串元素借用原数据直到render返回 | 有限支持，generic CMeta容器及其他迭代器未实现 |
| `items` | 有限支持native dict及Undefined，返回共享一次性游标，按唯一键首次插入顺序yield最终值的二元tuple；支持first/list消费、for/else及loop邻居/length，始终truthy且iterable而非sequence；==/!=按句柄身份比较且不消费，in/not in消费至匹配项或耗尽，句柄可作dict key；非mapping在消费时报RENDER，length/last/有序比较拒绝生成器，索引返回Undefined | 私有迭代器与外层循环别名已接入；generic CMeta Mapping、生成器repr仍未覆盖 |
| `trim(chars=None)` | 支持现有值的string转换、默认Unicode空白及Python U+001C–U+001F、自定义Unicode字符集、空集、位置或chars关键字参数；完整验证UTF-8，非法借用编码为METADATA | 复用Unicode/vstr，保留 Markup |
| `center(width=80)` | 支持位置或width关键字、int64可表示整数及bool宽度；按Unicode scalar计数并匹配Python奇数填充位置，宽度不足不截断；完整输出字节计入render工作区 | 非整数或不可表示的借用UINT宽度为RENDER；可表示宽度超字节容量为CAPACITY；保留 Markup |
| `reverse` | 支持零参数及空括号：字符串按Unicode标量反转、保留Markup；list/tuple/range/dict唯一键及borrowed sequence返回一次性反向迭代器，别名共享游标；已有iterator输入消费为反向list，可重复遍历。Undefined产生空迭代器 | 普通序列先有界物化，项数限max_nodes，即使只取first也受此限制；复用累计VALUE与字符串字节预算。非法输入/参数RENDER，非法UTF-8 METADATA；不提供迭代器repr、length、last、通用CMeta容器 |
| `sum` | 支持attribute/start位置或关键词绑定；默认start为0，空输入保留start，字符串start拒绝。支持现有可迭代输入，attribute为None时不投影；字符串按点分段（包括空段），Unicode Nd纯数字段按非负int64 key，其他参数直接作为key。数值累计采用checked int64加法及Python 3.12浮点阶段补偿；list/tuple start采用同类序列拼接，不修改输入 | 先有界物化，项数限max_nodes，属性最多64段且计入累计VALUE；超限/整数溢出CAPACITY，非法输入/缺失投影RENDER，完整属性UTF-8非法METADATA。沿用现有lookup边界；非Nd但Python isdigit为真的字符尚未复现其转换错误。浮点阶段退出边界依平台C long，对照固定CPython3.12.7；不承诺其他Python版本、任意精度整数或generic CMeta容器 |
| `join` | 支持d/attribute位置或关键词参数；复用sum属性路径与现有可迭代输入，普通元素转字符串，Undefined项为空字符串。关闭autoescape时结果为普通字符串；开启时安全分隔符或安全元素提升结果为Markup，非安全片段仅转义一次。保留Unicode及嵌入NUL，消费共享迭代器游标，不修改输入集合 | 先有界物化与转换，再拼接；沿用max_nodes、累计VALUE和字节预算，超限CAPACITY，参数/输入错误RENDER，非法UTF-8 METADATA。属性与字符串表示沿用现有边界，不提供任意宿主对象的Python __html__/__str__ 回调 |
| `attr` | 支持恰好一个字符串name位置或关键词参数，按完整名称读取namespace、range的start/stop/step以及CMeta struct字段；不拆点、不转数字、不退回dict或sequence下标。保留字段类型、Markup及namespace共享身份；已定义对象缺失属性返回Undefined，Undefined基对象报RENDER | 名称统一受max_string_bytes限制，超限CAPACITY；非法UTF-8或宿主元数据METADATA，非法参数RENDER。没有Python方法、反射对象、动态descriptor或__getattr__调用；此类非数据属性尚未映射，不声称完整Python对象模型 |
| `indent` | 支持数字或字符串width、first、blank；按Python splitlines识别换行并输出LF，默认不缩进首行和空行；保留Markup及前缀拼接语义 | 输出沿用render累计字节预算，无环境选项或新Unicode API；测试：test_jinja_native_functions.c |
| `truncate` | 支持length/killwords/end/leeway，默认255/false/"..."/5；按Unicode标量截取完整字符，非killwords只在最后一个ASCII空格处截词，无截断返回原值；支持Markup后缀组合 | leeway=None使用固定profile默认5，环境policies尚未提供；超字节预算CAPACITY，非法参数RENDER；测试：test_jinja_native_functions.c |
| `map` | 支持已有filter名称与位置/关键词参数，或attribute投影与default；数字路径、Unicode键与每层缺失替换沿用现有lookup。返回一次性迭代器，构造时保存参数、消费时调用filter；支持嵌套map及过滤链 | 仅当前可迭代值与已实现 filter/registered filter，生成器repr仍未覆盖；测试：test_jinja_native_functions.c |
| `select` / `reject` / `selectattr` / `rejectattr` | 支持真值或已有test及其参数、属性投影；只扫描到下一个匹配项，保持原元素与顺序；支持for/loop元数据、first/list/join及别名共享消费 | 参数准备在首次消费，falsy来源直接结束；数量、扫描、嵌套沿用现有预算，无新生命周期机制；测试：test_jinja_native_functions.c |
| `format` / 字符串 `%` | 共享位置/命名参数、flags、width/precision及星号、`%%`、数值/字符/str/repr/ascii转换；Unicode标量宽度与精度，Markup格式按上游规则转义；支持map及参数展开，数值取模不变 | 沿用数值表示、repr范围与字符串预算；格式/参数错误RENDER、超额CAPACITY；[原生功能测试](test/test_jinja_native_functions.c) |
| filter 参数、链、注册表 | Lemon 已接纳位置/关键词参数、括号调用和过滤器链，当前内建 length/count/default/d/first/last/abs/int/float/round/filesizeformat/format/wordcount/wordwrap/urlencode/xmlattr/string/list/items/trim/center/indent/truncate/reverse/batch/slice/sum/join/attr/replace/map/select/reject/selectattr/rejectattr/safe/escape/e/forceescape；关键词绑定由内建函数定义。安全过滤器可在任意表达式位置组合；已实现 filter 支持星号参数展开，environment 可注册 filter/test/global/tag | 求值时filter操作数和参数快照与内建call共享render的64个VALUE槽，LIFO准入/回收；延迟过滤器将参数复制到既有render VALUE区。嵌套超限CAPACITY，不保留通用求值器大栈帧等待递归操作数 |
| 带参数/点号/注册表 tests、globals、callables | 通用CALL先求值目标，再求值位置参数及*、关键词及**各一次，最后检查调用绑定；range及count/index、cycler仅位置参数，dict/namespace及joiner支持各自关键词。未知或非callable目标编译成功、执行报RENDER，短路不执行；显式重复keyword及keyword后普通位置参数为SYNTAX。loop直接调用沿用专用路径，与test共享参数展开；environment snapshot 支持注册 globals 与用户 callable | 宏/call执行已接入；注册 callable 不提供 Python 反射或动态替换 |
| callable值与 `is callable` | range/dict/namespace/cycler/joiner及已有绑定方法支持别名、集合存储、计算目标调用、真值、相等性及字典键。绑定方法比较接收者身份；不同range或切片不共用身份。callable test支持内建、绑定方法、Undefined与loop的能力判定；五种全局支持稳定repr/string | 绑定方法repr返回RENDER；不模拟Python地址、反射或任意对象方法。目标和参数共同使用64槽有界工作区；Undefined的callable判定为true不表示调用成功 |
| `cycler` / `joiner` | cycler接收非空位置选项，current/items/pos只读，next/__next__返回原值并推进，reset归零；joiner接受可选sep，首次调用返回空字符串，此后返回原始sep，used反映调用状态。实例/绑定方法别名共享状态，选项及分隔值保留身份和Markup；cycler不可调用，joiner可调用，二者非iterable | render统一拥有和释放，helper数复用max_nodes、选项复用集合额度；不提供实例或绑定方法地址repr、Python反射。测试：[原生功能](test/test_jinja_native_functions.c) |
| `range` | 支持 1–3 个 int64/bool 位置参数、负步长、惰性序列 repr/索引/切片/属性/真值/type tests/membership/equality/循环，以及单参数 count/index 方法；同名 context 可遮蔽内建；有序比较、非法参数或 index 未找到返回 `RENDER`，遍历/结果容量超限返回 `CAPACITY`。first-class count/index绑定方法已支持，别名保留接收者身份；keyword 调用可解析但在运行时拒绝，参数先求值再检查数量、类型与 keyword | 计划 |
| `dict(...)` | 支持零或一个位置参数加关键词，以及通用CALL参数展开；位置输入为native dict或当前可迭代的二元素键值对（含Unicode字符串对、items），关键词随后覆盖。保留首次相等键及插入顺序，重复键只更新值；返回独立快照，嵌套namespace/iterator保留共享身份；同名绑定遮蔽内建，参数先求值再报调用错误 | 键数限max_nodes，参数/表达式及累计VALUE沿用render预算；非法形状/不可哈希键/None/Undefined/错误数量返回RENDER，超限CAPACITY。尚无generic CMeta mapping；逐次不可变快照复制为有界O(n²) |
| 简单 `set name=expression` | 支持 ASCII 名称、括号名称、同层覆盖、Undefined 遮蔽及现有原生值类型；if 共享当前层，for 每轮/else 与 autoescape 隔离；不修改 CMeta root。绑定槽单独受 max_nodes 限制，超限 CAPACITY | 循环内禁止赋值 loop；LoopContext、宏值已支持，宿主callable注册尚未实现；内建和range方法值已支持 |
| 元组解包 `set a,b=rhs` | 支持交换、嵌套、重复目标最后写入、空元组；现有 list/tuple、dict 唯一键、range、Unicode 字符、borrowed sequence 与 items 迭代器；RHS 一次求值，形状全部校验后提交，类型或数量错误 RENDER。继承简单赋值作用域及 loop 限制 | 目标树独立限64节点，快照计入现有 VALUE workspace；generic CMeta 容器未实现 |
| 块式 `set` / `endset` | 支持嵌套捕获、名称/元组目标、现有过滤器链、局部scope与Unicode/NUL字节；过滤器先在capture局部层求值，再向父层赋值。autoescape开启时输入和最终值为安全字符串，关闭时过滤器可返回非字符串 | capture句柄单独限max_nodes，累计捕获payload单独限max_string_bytes；失败不泄露未完成捕获，保持原状态码 |
| namespace | 有限支持：原生dict、键值对序列、空字符串与keyword覆盖构造；属性/字符串下标读取、属性赋值、顶层tuple和capture目标、跨循环别名共享身份、身份比较及循环repr。普通对象属性写入返回RENDER，CMeta宿主保持只读 | 外部CMeta struct/map构造输入尚未支持；内建callable别名已支持；对象数及每对象字段数分别限max_nodes，写入快照累计计入VALUE工作区 |
| `with` | 有限支持：空头、名称/元组目标、多个初始化项、重复名称、嵌套；每项RHS在外层求值，按顺序检查解包后进入局部作用域发布。退出恢复外层，namespace/items保持共享身份；支持循环、capture、autoescape组合。拒绝尾逗号及属性目标 | 沿用已支持表达式与ASCII名称；最多64个pending叶绑定，活动槽限max_nodes；裸LoopContext作为值仍未实现 |
| `filter` block | 有限支持：已有filter链及参数、局部scope、嵌套filter/set捕获。参数看见body赋值，autoescape时捕获输入为safe；最终字符串原样写父sink，不再escape。非字符串最终值返回RENDER | 沿用捕获句柄max_nodes、累计max_string_bytes与活动深度限制；不因此支持尚未实现的filter或环境finalize钩子 |
| macro、call block | 支持公开编译/执行、位置/关键词及展开绑定、默认参数、varargs/kwargs、caller、递归、词法闭包、宏属性/repr及转义 | 单模板宏/call/命名block共享64函数上限、累计64形参/表达式；活动写绑定与保留激活受max_nodes限制，调用受max_render_depth限制；共享render实例下跨模板宏调用已接入。测试：test_jinja_cmeta.c及test_jinja_native_functions.c |
| `replace(old,new,count=None)` | 支持位置/关键词绑定、eager参数求值、现有值的字符串转换、非重叠匹配；count接受None、int64/bool，负数替换全部。空old在Unicode标量边界插入，保留NUL，不规范化。关闭autoescape时结果不保留Markup；开启时按old/new的安全标记决定是否先转义输入，Markup输入仅转义替换串，不转义搜索串 | 输入完整UTF-8验证并限max_string_bytes，转换/转义及结果共用累计保留字节上限。非法绑定/次数类型RENDER，超限CAPACITY，非法宿主UTF-8 METADATA。不支持任意宿主对象的Python转换回调；测试：test_jinja_cmeta.c的Jinja CMeta replacement及oracle/cases.json的replace_* |
| break/continue | 内建支持无参数标签，绑定最近的活跃词法循环；普通/过滤/递归循环及nested循环可用。跨with/autoescape/capture/filter退出时恢复循环作用域和转义状态，丢弃未完成捕获但不回滚已输出字节或namespace修改。continue复用FOR_NEXT，break不消费下一项；首轮break或全部continue按固定上游执行for-else | 上游oracle显式启用jinja2.ext.loopcontrols，本实现与do一样内建，不新增扩展开关。无目标循环、带参数或从recursive else跨函数控制外层循环为SYNTAX；循环else里的控制可指向仍活跃的外层非跨函数循环。沿用深度、节点、捕获字节预算，无新增分配 |
| 命名编译/loader | 支持opaque环境、配置快照、同步load/release、UTF-8显式长度名称、拥有的错误名及有界 compiled-template cache | env_load 按模板名命中 environment 级 FIFO cache，源码受max_loaded_source_bytes约束；不执行依赖；测试：test_jinja_environment.c、test_jinja_cmeta_header_cpp.cpp |
| include/import/from | 支持共享render内执行、上下文隔离、公开导出与别名；默认导入在单次render内复用，with context创建独立实例 | include支持候选名称和ignore missing；只忽略NOT_FOUND。模块与self为独立值类型，正文转换与模块repr分离；无跨render模块缓存。测试：test_jinja_environment.c |
| block、scoped、required、self | 支持单模板命名块、独立作用域、scoped局部上下文、嵌套/前向self块调用；执行未覆盖required块报RENDER，self保留创建上下文，块引用支持name属性；无祖先时super为Undefined | 与macro/call共享函数、调用深度及render存储预算；不模拟带Python地址的BlockReference repr。测试：test_jinja_native_functions.c |
| extends、super继承调用 | 支持父模板延迟执行、多级block覆盖、super与super.super、scoped上下文及required检查 | 共享render预算，拒绝继承环和重复执行extends；作用域恢复保留实际定义实例。测试：test_jinja_environment.c；后续设计见EXTENSIBILITY_DESIGN.md |
| raw/endraw | 支持原文区域、首个合法结束标签、非嵌套语义及 +/- 空白控制；标签空白与裁剪使用 Unicode White_Space 加 Python 信息分隔符；不解释内部表达式、语句、注释 | 复用 re2c raw 扫描入口和有界 TEXT 指令；未结束返回 SYNTAX |
| 扩展 tag | 有限支持：启用 `EXTENSION_TAG_CUSTOM` 后，按 `{% tag(expr1, expr2) %}` 调用 environment snapshot 中注册的 tag；callback 结果丢弃，失败终止 render | 不提供 Python extension API、宿主对象写入或自定义 block parser |
| undefined policy | 支持默认 Undefined 语义，并可由环境选择 `StrictUndefined`；默认模式下普通输出为空、条件为 false，严格模式在输出、布尔判断、比较和字符串转换等消费点返回 `RENDER`，`is undefined`/`is defined` 仍可用于判定；已定义 base 的无效 `[]` lookup 生成 Undefined，而继续索引 Undefined 返回 `RENDER` | 严格模式仍不模拟 Python 异常类型与完整异常消息 |
| autoescape | 支持表达式块与嵌套恢复；只转义不安全的插值，文本不转义，默认关闭；环境可按命名模板选择初始状态，include/extends 子模板使用自身选择结果并在返回时恢复父模板状态；受控制深度上限约束 | 选择器仅返回布尔状态，不支持按内容/上下文动态选择 |
| sandbox | 不支持 | 计划：限制 CMeta lookup 与原生回调 |
| async、native types | 不支持 | 首个 profile 不支持 |
| i18n (`trans`/`pluralize`) | 支持 | 通过环境翻译回调处理命名占位符；需显式启用 `JINJA_CMETA_EXTENSION_TAG_I18N` |
| 自定义 extension | 有限支持：仅支持注册 callable tag/filter/test/global 与 expression-call tag；不支持动态 Python 扩展加载 | sandbox、async 不属于首个 profile |

## Oracle Environment

`test/oracle/jinja_oracle.py` 是唯一的 Python oracle 入口。它先核对固定依赖版本，版本
不符便 fail fast。每个 case 使用新的 Environment，配置如下：

```python
Environment(
    block_start_string="{%",
    block_end_string="%}",
    variable_start_string="{{",
    variable_end_string="}}",
    comment_start_string="{#",
    comment_end_string="#}",
    line_statement_prefix=None,
    line_comment_prefix=None,
    trim_blocks=False,
    lstrip_blocks=False,
    newline_sequence="\n",
    keep_trailing_newline=False,
    optimized=True,
    undefined=Undefined,
    finalize=None,
    autoescape=False,
    loader=None_or_case_DictLoader,
    cache_size=0,
    auto_reload=False,
    enable_async=False,
)
```

`DictLoader` 只属于测试夹具，用于给 include/import/inheritance case 提供确定性模板集合；
它不规定 C API loader 的形状。oracle 入口把 stdout/stderr 显式配置为 UTF-8，避免宿主
Windows console code page 使非 BMP scalar 的结果输出失败。

## Unicode 与字节契约

`jinja_cmeta_compile()` 先应用 `JINJA_CMETA_MAX_TEMPLATE_BYTES` 字节上限，再用 Salts
`vstr_utf8_invalid_offset()` 对完整 borrowed source 做严格 UTF-8 准入。非法 overlong、
truncated、UTF-16 surrogate 或大于 `U+10FFFF` 的序列返回 `JINJA_CMETA_ERR_SYNTAX`；
`error.offset` 是首个非法字节的零基 byte offset。通过准入后，template re2c lexer 只按
delimiter 扫描字节，不维护第二套 UTF-8 decoder。

原始文本及未转义字符串内容保持输入的 UTF-8 bytes；当前 profile 不做 NFC/NFD
normalization、grapheme cluster 分割、Unicode case folding 或 locale collation。因此
预组字符 `U+00E9` 与 `U+0065 U+0301` 保持不同字节序列。template 大小、source span、
错误位置和 render string 限额均以 bytes 为单位，不以 code points 或 grapheme clusters
为单位。

固定宽度 `\xHH`、`\uHHHH` 和 `\UHHHHHHHH` escape 在 compile finalization 阶段解码为
canonical UTF-8 bytes。ASCII hex 位不足或非法、UTF-16 surrogate 以及大于 `U+10FFFF`
的值返回 `JINJA_CMETA_ERR_SYNTAX`。这项严格 scalar 规则优先于 Python 可暂存孤立
surrogate 的实现细节，以维持 Jinja CMeta 输出始终为合法 UTF-8 的不变量。解码后的长度、
容量和 render 限额仍以 bytes 计算：例如 `U+1F600` 占 4 bytes。`\x00` 的 NUL 可由
`jinja_cmeta_render()` 通过带显式长度的 callback 保真输出；无独立长度的
`jinja_cmeta_render_string()` 不适合作为 embedded-NUL 的消费接口。

表达式identifier与空白使用上述Unicode属性规则，不读取进程C locale或宿主code page。
公开语句关键字分派使用完整re2c词法token，参数外围按Unicode空白处理。
普通tag的`-`裁剪、raw裁剪与字符串解码也使用Unicode空白，无legacy profile或兼容分支。
公开编译器与私有模板树已共用逐标签扫描器，statement支持`+`控制标记；
公开编译器统一创建owned整树。树使用按需CSTL节点存储，不再限制256节点；
节点硬上限由16 MiB源字节上限推导，成功替换/失败保留旧树，compiler统一清理。
parser112项/2131断言通过；公开全树编译与环境词法选项已接入，
loader、filter/test注册表与环境finalize等运行时能力仍未完成。

## 未实现的Unicode大小写过滤器（2026-09-10审计）

`upper/lower`尚未接入。HIGH，事实：Salts的`utils/src/tstr.c`中tstr_lower/tstr_upper只转换
ASCII范围；当前unicode库的属性查询与UTF-8扫描不提供完整case mapping，不能用它们冒充Python转换。
固定上游`jinja2/filters.py`的do_upper/do_lower使用soft_str后调用字符串大小写方法。
新增10条case_* oracle覆盖多标量扩展（ß、ﬃ、İ）、上下文Final_Sigma、组合符、补充平面、
Markup、安全输出、非字符串转换、空参数展开、非法参数及NUL；固定oracle583条预期全部通过。
这些是预期记录，不是C端实现或一致性覆盖。

实现约束：共享Unicode能力需支持一对多映射及上下文条件；不能逐字节tolower，也不能只用简单单码点映射。
输出采用既有render-owned字符串预算，先checked计量再发布，完整验证输入UTF-8；保留Markup来源，
默认无locale依赖、不做规范化。Unicode17数据与固定Python Unicode版本差异必须继续显式记录。
下一实施批次先建立C端失败测试，再实现共享映射与Jinja适配；本批没有新增生产API或依赖。

## 错误分类

| C 状态 | 发生阶段 | 契约 |
| --- | --- | --- |
| `JINJA_CMETA_ERR_INVALID_ARGUMENT` | API admission | view、descriptor、root、renderer 或输出参数非法 |
| `JINJA_CMETA_ERR_SYNTAX` | lex/parse | 输入不是合法 Jinja 语法或控制块不匹配 |
| `JINJA_CMETA_ERR_UNSUPPORTED` | profile admission | 输入是合法或可识别的 Jinja 构造，但不在所选 profile 内 |
| `JINJA_CMETA_ERR_CAPACITY` | compile/render | template bytes、token、AST、嵌套、迭代、workspace 或输出预算耗尽 |
| `JINJA_CMETA_ERR_OUT_OF_MEMORY` | compile/render | 在合法容量预算内仍无法分配 |
| `JINJA_CMETA_ERR_METADATA` | CMeta adapter | descriptor、shape、borrowed view 或元素布局不合法 |
| `JINJA_CMETA_ERR_RENDER` | evaluator/output | renderer、filter/test/callback 或求值运行时失败 |
| `JINJA_CMETA_ERR_NOT_FOUND` | loader | 本次请求的模板名称不存在 |
| `JINJA_CMETA_ERR_LOADER` | loader | 未配置loader，或宿主I/O/权限等加载失败 |

规则：能识别但未纳入 profile 的构造必须返回 `UNSUPPORTED`，不得近似执行；结构损坏、
非法 token 序列或未闭合结构返回 `SYNTAX`。oracle 把上游异常归一化为 `syntax`、
`undefined`、`loader`、`security` 或 `runtime`，不比较易随 patch 版本变化的完整消息文本。

## Parser 决策

原生 parser 使用仓库已有的 re2c/Lemon 生成链，但不照搬 Mustache 实现：Mustache 当前
是手写扫描器。迁移按可独立验收的切片进行：当前 compile 路径使用 re2c
识别 delimiter；完整 UTF-8 准入和首个非法 byte offset 统一复用 Salts `vstr`。插值与 `if`/`elif` 共用
64-entry 固定栈的 Lemon 点路径/平衡括号 grammar，条件另允许两种 Jinja 大小写布尔字面量、有符号十进制或
`0b`/`0o`/`0x` 前缀 `int64`、十进制/指数 double、`none` / `None`、受限字符串字面量、可重复一元 `not`、有界数值算术、零参数 `is`
tests、`and` / `or`、条件表达式，以及点路径/布尔/整数/浮点/字符串叶节点间的六种比较、`in` /
`not in`、比较链和括号嵌套比较。
grammar 的层级为 conditional → `or` → `and` → `not` → comparison → additive → multiplicative → power → test → unary → postfix → primary；test 在一元算术后、二元算术和比较前结合，逻辑运算从左到右短路并返回被选
operand，而不是强制返回 bool。Lemon 把每个复合表达式保存为最多 64 节点的后序树，子节点索引
必须小于父节点；比较链另受 64-step 固定容量约束，任一容量用尽立即返回 `CAPACITY`。比较遵循 Jinja 的 comparison-before-`not`
优先级。同类型 literal 在编译期折叠；其他已接纳比较由
Lemon 保留两个 typed operand、operator 和 `not` 次数，compile 层再把路径及解码后的字符串复制进
模板私有只读 AST。render evaluator 从当前 Jinja 循环上下文 向 parent chain 查找路径首段，后续路径段
只在已选中的 struct 中解析。运行期数值支持 CMeta bool、有符号与无符号整数及 float；混合整数/浮点
比较在整数边界处使用精确 ordering，NaN 保持 unordered；字符串按显式长度的 canonical UTF-8 bytes
比较，不引入 normalization 或 locale collation。
两个 undefined 相等，undefined 与已定义 scalar 或异类型 scalar 不相等；异类型/undefined ordering
以及 enum、object、sequence operand 返回 `JINJA_CMETA_ERR_RENDER`。比较链把初始 operand 和
有序 step 保存在固定容量结构中；render 从左到右求值，每个 operand 恰好一次，首个 false step
短路后续 operand。括号比较结果通过严格后序 child index 参与外层比较。整数扫描按 token 前缀选择
2/8/10/16 radix 并使用 checked multiply/add，错误 digit/separator 返回 `SYNTAX`，越界立即返回 `CAPACITY`。
算术节点同样使用严格后序 child index；re2c 以 operand/operator 上下文区分相邻 signed literal 与
无空白 binary sign。render 把 bool、有符号整数和不大于 `INT64_MAX` 的无符号整数纳入 checked `int64`
路径；整数真除法保留商/余数并正确舍入为double，float、混合算术和负整数指数走double路径。
`//` 与 `%` 显式修正 C 的 truncation 结果以
匹配 Jinja/Python floor 语义；`INT64_MIN % -1` 返回可表示的 `0`，对应 floor quotient 超范围则返回
`CAPACITY`。power 按 Jinja 左结合；当前值模型无法表达的 complex 结果返回 `RENDER`。
membership 对字符串调用 Salts `vstr_contains()`，比较解码后的 exact UTF-8 bytes；合法 UTF-8
不会在 continuation byte 中产生伪 code-point 匹配。sequence membership 直接扫描 borrowed contiguous
view，以既有 scalar equality 比较 element；非空 view 先验证 data、stride、element descriptor、
element size 和末元素 offset，再以 render `max_nodes` 作为当前实现的 scan-step 硬上限。
后缀 `[]` 节点保存严格后序的 base/key child index。CMeta struct 只接受 string key，borrowed
sequence 与 UTF-8 string 只接受 bool/可表示为 `int64` 的整数 key；负值以 checked magnitude 从尾部
归一化。字符串通过 Salts Unicode scanner 完整验证并按 Unicode scalar 定位，返回原字节序列的
borrowed subview，不进行 grapheme segmentation 或 normalization。已定义 base 的 missing、越界或
不适用 key 返回内部 Undefined；继续索引 Undefined 返回 `RENDER`。非法 struct/sequence/string
metadata 返回 `METADATA`。点号由同一 postfix 层接纳；未分组的普通 dotted path 仍折叠为既有紧凑 PATH，
`[]` 或分组结果之后的 `.identifier` 则保存为严格后序 attribute 节点。该节点仅支持 CMeta struct field，
复用相同 metadata/address 校验；missing attribute 返回 Undefined，Undefined base 返回 `RENDER`。list/tuple/dict
literal 由同一 postfix/比较/test 层组合；切片的生命周期与容量协议见
[切片实现计划](../docs/superpowers/plans/2026-09-09-jinja-slices.md)。当前不实现 collection comprehension/unpacking、generic CMeta
map/sequence、Python 风格 item/attribute fallback 或 descriptor/method。
re2c 将相邻单/双引号串（允许Unicode空白分隔）接纳为单个token；parser span保留各段引号，compile finalization逐段解码并合成单个常量。
转义不能跨引号边界，postfix作用于合并结果，总解码字节受同一预算约束。compile finalization 解码 simple escape 和固定宽度
`\xHH` / `\uHHHH` / `\UHHHHHHHH`，并对解码后总字节数做 checked accumulation。
未知ASCII转义保留反斜杠；反斜杠接非ASCII标量按上游ASCII backslashreplace步骤输出字面的 `\x`/`\u`/`\U` 十六进制文本。
八进制转义读取1–3位并输出UTF-8；实际CR/CRLF规范化为LF，反斜杠接实际换行输出零字节。字符串取反在解码后求真值。
named Unicode escape 通过共享 Unicode 17 数据查名并输出单个scalar；未知名称返回 `SYNTAX`，
不接受命名序列。固定宽度 escape 的格式或 scalar 非法同样返回 `SYNTAX`。
表达式 parser 为 re2c 的 sentinel 契约建立调用期 NUL 结尾 scratch copy，因此 tag body
即使是原模板中的非结尾 subview，也不会越过 view 边界读取。
逻辑树在 render 时使用当前到 parent 的同一 lookup 规则；未被选择的 branch 不解析路径、
不执行比较，也不产生中间 provider node。原生 TEST 对所有条件（包括普通点路径）只求值一次，
按 truthiness 跳转，不遍历 sequence 或压入对象上下文。普通逻辑插值保留被选 operand，
Undefined 输出空字符串。普通路径与表达式都由 Jinja 运行时解析，不再生成其他模板语言。
条件表达式同样保存严格后序 child index：
true value、test 以及可选 false value 都在父节点前发布。
render 先且仅先求值 test，再只递归到选中的 branch；省略 `else` 的 false 路径生成内部 undefined，
不会读取 true branch。表达式自身低于 `or`，连续 `if` 按 Jinja 从左到右构造，`else` branch
递归接纳 conditional。语句入口拒绝未分组的 conditional 根，而括号内表达式仍由同一 AST evaluator
执行。
`is` test 节点保存一个严格后序 operand index 和固定枚举，不保存字符串注册表键。render 只求值
operand 一次，再按 internal value kind 或已验证 CMeta descriptor kind 分类；`is not` 在结果上取反。
当前 CMeta struct/map 判为 mapping；string/bytes/struct/map/sequence 与 Jinja borrowed sequence view
判为 sequence；set 只加入 iterable。默认 Undefined 同时判为 sequence 和 iterable，以匹配固定 Jinja
3.1.6 Environment。该类型判定不承诺 generic CMeta map/sequence/set 已能 lookup 或迭代。
位置参数复用有界 collection item 表，运行时先求值 operand，再按源码顺序求值参数，最后检查
数量并调用 test；目前带参内建为 divisibleby。点号或 registry-dependent test 返回
`UNSUPPORTED`；未给 test 名及未分组连续 `is` 返回 `SYNTAX`。
filter 与 `for` payload 由原生表达式 parser 解析，模板结构直接生成有界指令。Jinja 前端采用以下
混合结构：

1. re2c 在 template-text 与 tag-expression 两种模式间切换；
2. 原始文本直接生成有界 text node，不逐字符送入 Lemon；
3. Lemon 解析 expression/statement token，并负责优先级；
4. `.re` / `.y` 是维护源，生成的 C/H 文件只存在于 build tree；
5. source span 是 compile 输入中的 byte offset；需要逃逸 compile 调用的数据由模板复制；
6. token、AST node、字符串字节、parser stack 和嵌套深度均有硬上限。

模板编译结果由原生指令、独占字节池与表达式 AST 组成，无 Mustache lowering 或兼容执行器。
TEXT 不再次解析，OUTPUT 使用 Jinja 输出层；TEST/JUMP 选择分支，FOR_BEGIN/FOR_NEXT 管理循环。
指令上限 65536、字节池上限 32 MiB；跳转目标为指令索引，数据引用为 offset/length，
构建用有界 CSTL Vec/tstr，完成后复制为精确长度只读存储。`max_render_depth` 限制实际执行的
词法控制深度，0 使用 64。每次 render 的循环栈与节点工作区独占，错误报告当前指令源偏移。
与旧执行器相比，条件不再分配 section context，输出节点计费按原生路径重新核算。

## 所有权与资源协议

- compile 输入是临时 borrowed view，只保证在 `jinja_cmeta_compile()` 返回前有效。
- compiled template 独占需要跨调用保存的 AST、字符串表和 source metadata。
- 字符串字面量在 compile 返回前解码并复制到 compiled template 的连续只读存储；AST view
  在 `jinja_cmeta_release()` 时失效，不借用调用方 source。
- 同类型字符串 literal 比较的两个 source view 只在 compile 内借用；按解码后长度分配的临时
  buffer 在折叠成功或失败时均释放，compiled template 仅保存最终不可变布尔节点。
- deferred comparison 的路径 bytes 与解码后字符串 bytes 在 compile 返回前复制到 compiled
  template；operand view 随 `jinja_cmeta_release()` 失效，不借用调用方 source。
- call argument index range 由 compiled template 独占；`loop.changed` 的 shallow value snapshot
  由单次 render workspace 独占，只借用同次 render 内地址稳定的 template/input/context 数据。
  每个 iterable wrapper 保存自己的 history range；同一 loop 的调用点共享，nested/sequential loop
  分离。snapshot arena 的容量单位为 value，硬上限与 `max_nodes` 相同，满额返回 `CAPACITY`。
- comparison chain 的 step array、child index、路径 bytes 与解码后字符串 bytes 均由 compiled
  template 独占；发布前校验 step 范围和 child 后序关系，release 时统一释放。
- membership scan 借用 render 输入的 sequence element storage，仅在同步调用中读取；不保存 element
  pointer，不创建 intermediate provider node，最终 boolean 仍只占一个 workspace node。
- item lookup 的 struct field、sequence element 与 UTF-8 scalar view 只在同步 render 栈中借用；每个
  lookup 对 base/key 各求值一次，不分配 intermediate provider node，最终表达式仍只占一个 workspace node。
- postfix attribute 的 identifier bytes 由 compiled template 拥有；base 只求值一次，CMeta struct field
  只在同步 render 栈中借用，最终表达式仍只占一个 workspace node。
- logical tree 的 path bytes 与解码后 string bytes 同样在 compile 返回前复制；compiled child
  index 在发布模板前验证为严格后序引用，模板成功返回后保持只读。
- conditional tree 复用相同的 owned leaf storage；test 与 branch intermediate value 只在同步
  render 调用栈中借用，未选 branch 不访问，最终仍只分配一个 provider node。
- list/tuple element index array 与 dict key/value pair array 由 compiled template 独占；render
  collection value 只借用该不可变 storage 和当前 CMeta context。dict 的重复键、唯一顺序与 equality
  使用最多 31 个 raw entry 的有界 O(n²) scan，不建立无界哈希表或跨 render cache。
- 每个成功 materialize 的 iteration child 在同次 workspace 内保存只读 `(index0,length)`，并借用
  产生它的 address-stable iterable wrapper；synthetic `loop` object、scalar field、邻项与边界 Undefined
  sentinel 都受 `max_nodes` 限制。native expression 沿 parent chain 读取最近一层 metadata，通过同一
  wrapper 的 checked index 取得 `previtem/nextitem`，所有中间值与借用均不逃逸同步 render。精确 loop
  alias 使用合法 Jinja identifier 无法产生的私有名称解析回最近 iteration node，避免嵌套条件 section
  改变别名所指对象。外层别名和显式成员路径通过native PATH首段沿活动iteration父链查找；
  同名绑定选最近层，for的else排除自身绑定。别名字符串由编译模板持有。
- None/test 节点不借用额外 source bytes；test operand 通过严格后序 index 引用，只在同步 render
  调用栈求值一次，最终布尔值才占用一个 provider node。
- render 输入对象、descriptor、字符串与 sequence storage 由调用方拥有，在同步 render
  返回前必须不可变且地址稳定。
- 每次 render 独占自己的 workspace；workspace node 的 parent link 只在该次同步 render 内有效，
  用于当前到外层 context 的只读查找。arithmetic/comparison/logical/conditional 求值的 intermediate value
  全部在调用栈借用且不分配，复合表达式只为最终结果占用一个 node；compiled template
  匿名编译产物只读时允许并发render；关联环境的模板遵守环境单线程同步契约。
- 容量满额一律立即返回 `CAPACITY`，不覆盖、不静默丢弃、不转为无界分配。
- 比较与tuple键验证共用render独占的`max_value_visits`/`max_value_depth`：零值分别选1048576/64，
  深度配置超过64在输出前`INVALID_ARGUMENT`。成功入栈才计一次访问，根/叶均计活动深度，
  每个成功入口在错误路径也恰好退栈；访问数不在表达式/循环/宏入口重置。满额立即`CAPACITY`，
  不产生新的堆存储或跨render状态。辅助计数空间O(1)，比较栈O(depth)，不保证共享DAG线性时间。
- streaming renderer 失败前已经发出的 bytes 不回滚；string renderer 失败时不交付部分结果。

环境/loader接口已确认并实现：opaque handle持有不可变配置快照，模板借用环境，
宿主先结束render并释放模板，再销毁环境；回调userdata由宿主持有到销毁。
扩展后的ERROR要求所有调用方重编译，无旧ABI兼容层或违约调用检测状态机。
注册表接口已实现为环境级冻结快照；函数回调只接收Undefined/None/bool/int64/double/UTF-8
string标量，参数view仅在同步回调期间有效。返回string在回调返回前复制进本次render的累计
字符串预算，环境注册表不持有跨render可变执行状态。模板组合继续由单次render共享provider
实现，不引入跨render共享session。

## 验收门槛

`jinja-3.1-cmeta-v1` 只有同时满足以下条件才能标为可用：

- matrix 中每个“支持”项均有成功、失败、边界和交互测试；
- 与固定 oracle 的输出和归一化错误分类一致；
- unsupported 构造明确失败，不做 Mustache 近似降级；
- MSVC、Clang、可用 sanitizer、安装导出和外部 consumer 验证通过；
- 公开文档完整说明 profile、ownership、limits、errors 和 thread model；
- benchmark 报告 compile/render 的输入口径，性能结论来自同机同配置实测。
