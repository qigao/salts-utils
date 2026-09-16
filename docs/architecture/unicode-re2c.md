# re2c Unicode 模块架构决策

## 背景

Jinja 的 Unicode identifier/whitespace 语义需要版本化 character properties。Salts `vstr` /
`tstr` 已提供 byte view、UTF-8 准入和字符串生命周期，但不提供 `XID_Start`、
`XID_Continue` 或 `White_Space`。若把 property 表写进 Jinja，会让 Unicode 数据、错误语义和
升级成本绑定到单一模板引擎，也无法由其他高层工具复用。

## 候选方案

| 方案 | 性能 | 依赖/体积 | 维护与兼容 |
|---|---|---|---|
| Jinja 私有手写 decoder/property tables | 可控 | 无新 target | 与 `vstr` 准入重复，其他模块无法复用 |
| 只依赖 `vstr` UTF-8 API | 可控 | 最小 | 无法表达版本化 identifier/whitespace property |
| 引入 ICU | 成熟但边界较重 | 新运行时依赖和明显体积 | 能力远超当前范围，构建/部署成本高 |
| 独立 re2c Unicode target | 单 scalar O(1)，生成 DFA | 仅构建期 re2c；运行时只依赖 Core | 数据版本与模板语义解耦，可独立测试和升级 |

选择独立顶层 `unicode/` 模块，导出 `Salts::Unicode`。这是直接 C API，不引入 factory、
全局 registry 或可变 context；当前问题没有需要这些抽象的运行时 variation。

## 边界与依赖

```text
Salts::Core (vstr)
       ^
       |
Salts::Unicode       generated from re2c 4.6 + pinned Unicode 17 properties
       ^
       |
Jinja integration (follow-up)

Mustache remains independent and has no Unicode dependency until it needs this API.
```

- `Salts::Unicode` 只拥有算法代码和固定 property 语义，不拥有输入/output 内存。
- caller 是 cursor、input bytes 和 result storage 的唯一事实源。
- scanner 每次调用从 cursor 读取一个 scalar；成功后一次性提交 cursor/result。
- 失败无状态迁移，因此不需要 rollback、补偿或 fallback。
- generated C 文件只存在 build tree；安装包不依赖 re2c 或 property source file。

## 数据与生成协议

Unicode 行为固定为 17.0.0。re2c 4.6 的 `unicode_properties.re` 由官方
`DerivedCoreProperties.txt` 与 `PropList.txt` 生成；配置阶段检查最低工具版本，并从
`RE2C_ROOT` 下的安装目录读取 stdlib 文件。

生成命令启用 UTF-8 encoding 和 `encoding-policy=fail`。DFA 先匹配 `XID_Start`，再匹配
`XID_Continue`、`White_Space` 和任意其他合法 scalar；最后的 default byte rule 只表示非法
UTF-8。因为完整 property DFA 的入口最长 lookahead 是 4 bytes，模块把当前最多 4 input
bytes 复制到固定栈 scratch，并以非法 byte 填充 4 bytes padding。这样既不借用越界，也不会
把短但合法的终止 scalar 误判为 truncated。

## 接口、错误与线程语义

### 可打印字符查询提案（2026-09-10，待公开 API 批准）

MED / 事实：Jinja当前容器repr只转义ASCII控制字节，U+0085、U+00A0、U+200B、U+2028、
私用/未分配及部分非BMP标量均原样输出。现有Unicode扫描仅提供XID和White_Space，
无法区分可打印组合标记与不可打印格式字符；这些集合也不能互相替代。
规则参考[Python isprintable](https://docs.python.org/3.12/library/stdtypes.html#str.isprintable)：
General_Category属于L/M/N/P/S，或U+0020时为真，其余有效标量为假。
这不是显示宽度、grapheme、终端安全或HTML安全判断。

建议新增独立查询，尚未声明或实现：

```c
salts_unicode_status salts_unicode_is_printable(uint32_t scalar, int *result);
```

固定Unicode17；re2c复用固定版本的unicode_categories.re，以UTF-32单标量规则
`(L | M | N | P | S | [\x20])`分类，生成到原build tree。合法标量均返回OK并写0/1；
NULL输出、surrogate或大于U+10FFFF返回ERR_INVALID_ARGUMENT，输出不变。
参数值按值传递，结果由caller持有；无借用存活、堆分配、可变共享状态、锁或初始化/关闭过程。
并发调用只读固定DFA；分类为单标量O(1)时间和栈空间，不需要新增容量或重试路径。

候选比较：向已有scalar.properties追加位会改变所有调用者可见的mask，并增加每次词法扫描成本；
独立查询只让需要该语义的调用者付出成本。Jinja私有分类表会重复Unicode事实源；ICU引入新依赖。
因此保留已有扫描结果/结构体与所有旧property位，仅扩展Unicode库的公开符号集合。
Jinja继续单向依赖Unicode；Mustache不变。tstr/vstr继续管理字符串，不承担分类策略。

Jinja接入仅调整jinja_dump_string_repr：按已有UTF-8扫描取标量，保留引号选择、反斜线及
tab/LF/CR短转义；其他不可打印标量按<=FF、<=FFFF、其余分别写4/6/10字节的
`\xhh`、`\uhhhh`、`\Uhhhhhhhh`。普通string输出、HTML转义策略不变。
原始字符串是本次同步render有效的不可变借用；固定栈escape buffer只在同步writer调用期间借用，
writer不得保留它。原样区间仍借用原始存储；repr不额外复制整串或新增全局构建状态。
所有输出沿原sink的pending/retained/capture/final预算准入，超限CAPACITY，writer失败立即传播；
流式已写前缀不回滚，render_string不发布部分结果。原始错误offset与重入恢复仍由执行边界负责。
时间O(input bytes + emitted bytes)，额外空间O(10)，不声称性能改善。

版本边界：本地oracle为CPython3.12.7/Unicode15；例如U+1FAE9在Unicode17已分配为So，
在旧oracle仍为Cn。不能为匹配旧oracle偷偷降级Unicode库，也不能把新分配字符的差异当实现通过。
共同版本字符用固定Jinja/MarkupSafe差分；Unicode17特有字符与全码点分类用官方
[UnicodeData.txt](https://www.unicode.org/Public/17.0.0/ucd/UnicodeData.txt)独立验证，分母分别报告。

迁移：新Jinja二进制需要同时发布带新符号的Unicode库；不提供缺符号fallback，不改已安装SDK。
使用既有C/C++测试及独立临时安装consumer验证导出；旧结构体布局、配置/数据格式不变。
回滚必须一起撤回新Jinja调用和Unicode符号，不能单独降级Unicode DLL。
验收还包括全部ASCII、类别/范围边界、非法scalar输出不变、UTF-8/NUL、Markup/autoescape、
嵌套容器/宏重入、恰限/超限、sink失败及重渲染；三配置Unicode/Jinja回归后才能关闭issue #26该项。

### 名称查询扩展

经用户确认新增 `salts_unicode_name_lookup(vstr name, uint32_t *out_scalar)`，无既有结构体布局变更。
官方 Unicode 17 DerivedName/NameAliases 是唯一名称事实源；离线生成只读排序表及派生范围，
与 re2c 属性 DFA 并存。名称生成头随源码分发，属性 DFA 仍生成到 build tree。
数据许可、版本、hash、再生成及完整验证命令见 [数据说明](../../unicode/data/README.md)。

输入是显式长度借用 ASCII 名称，最长 88 字节；不保留输入，不分配内存，不修改全局状态。
普通名称二分查询，派生名称检查固定范围；时间 O(L log N + R L)、栈空间 O(89)，N/R 为固定表规模。
成功仅写一个 scalar；未知名称 `NO_MATCH`，非法指针参数 `ERR_INVALID_ARGUMENT`，失败输出不变。
Jinja 词法校验与字面量解码共用该接口；Mustache 不增加依赖。
迁移不涉及存储/配置格式，但先前不支持的命名转义现在可输出，未知名称改报语法错误。
回滚需一起撤销 Jinja 调用、新 API 和生成表；不能只删除数据留空实现。

### 原有扫描接口

- `salts_unicode_utf8_next()`：borrowed explicit-length view + in/out byte cursor。
- `salts_unicode_scalar_properties()`：对 caller 提供的 scalar 做同一 DFA property 查询。
- `salts_unicode_version()`：返回 immutable static version string。
- `SALTS_UNICODE_END` 与错误分离；invalid argument 和 invalid UTF-8 可区分。
- 错误时 cursor/result 不变；顺序扫描时 cursor 就是首个未接受 byte offset。
- 无全局可变状态、无 allocation、无锁；API 对 immutable input 可并发调用。

### M1 字符语义操作

M1 在同一 `Salts::Unicode` target 上增加三项零分配操作，不复制 Salts Core 已有的 UTF-8
length、slice、find 或 encode API：

```c
salts_unicode_status salts_unicode_trim_whitespace(vstr input, vstr *output);
salts_unicode_status salts_unicode_xid_span(vstr input, size_t start, size_t *end);
salts_unicode_status salts_unicode_is_xid(vstr input, int *result);
```

`trim_whitespace` 扫描完整输入并返回借用同一 storage 的连续 subview；只移除 Unicode 17.0.0
`White_Space`，保留内部 whitespace。`xid_span` 要求 `start` 位于 scalar 边界，首 scalar
必须是 `XID_Start`，后续消费 `XID_Continue`；到达输入末尾返回完整 byte end，不把 `_` 或
模板关键字等语言策略写入 Unicode 层。`is_xid` 扫描完整输入，空输入和 property 不匹配是
正常 false，malformed UTF-8 是错误。

所有输出只在成功时提交；`END`、`NO_MATCH` 或负错误状态不修改 caller output。返回的
`vstr` 不延长 input 生命周期，在来源 `tstr` append、resize、clear、free 或其他可能改变
地址/内容的操作后立即失效。函数无全局状态，因此多个线程可并发读取 immutable input，
但不得与来源 `tstr` 写入并发。每项操作时间 O(input bytes)、额外空间 O(1)。

### Core 字符串文件命名迁移

Salts Core 的 canonical 文件改为 `utils/include/tstr.h`、`utils/src/tstr.c`、
`utils/include/vstr.h`、`utils/src/vstr.c`。公开符号、ABI、ownership 和 UTF-8 行为不变。
旧 header 路径暂时保留为只包含 canonical header 的 deprecated forwarding layer；旧 `.c`
文件不保留，避免双实现事实源。`C:/projects/cpp/turbonet` 内受控源码统一改用新 header，
生成目录、依赖目录和 `.git` 元数据不参与机械替换。

兼容风险是外部消费者若依赖源码文件路径而不是 `Salts::Core` target，会需要更新构建脚本；
仍包含旧 header 的消费者继续可编译。验证必须覆盖新旧 header C/C++ smoke、Salts Core
focused/full tests、安装 package consumer，以及 SaltsUtils Unicode/Jinja 相邻回归。

## 架构影响与迁移

- 新增 `Salts::Unicode -> Salts::Core` 单向公开依赖和安装 target/header。
- 当前 Jinja/Mustache 用户可见行为不变；Jinja Unicode identifier/whitespace 将在后续切片显式
  链接该 target，并同步更新 legacy/profile 兼容矩阵。
- 不改变已有数据格式、配置格式或模板输出。
- 从 Jinja 私有实现迁移时只替换 lexical classification，不迁移 AST/runtime ownership。

## 回滚与验证

回滚可删除 `unicode/`、根目录 `add_subdirectory(unicode)` 与 FindTools 的 Unicode admission
检查；现有模板 target 没有反向依赖，因此不涉及数据迁移。验证范围包括 malformed UTF-8、
embedded NUL、BMP/non-BMP、property 正反边界、C/C++ header、Jinja 相邻回归以及安装后
consumer 链接。
