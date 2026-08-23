# TurboParser 依赖边界

## 背景

TurboParser 从 TurboUtils 的 `parser/` 子树独立出来。解析器继续复用
TurboUtils 的错误、字符串、容器、文件、内存映射、正则和平台能力，但
TurboUtils 不反向依赖 TurboParser。此边界用于避免两个安装包导出同名 target，
并允许解析器和基础工具库分别发布。

## 选择

依赖方向固定为：

```text
application -> TurboParser -> TurboUtils::Core
                         \-> TurboUtils::TinyTest (tests only)
```

TurboParser 拥有并导出：

- `TurboParser::QueryVM`
- `TurboParser::CYaml`
- `TurboParser::Parser`
- `TurboParser::Cron`
- `TurboParser::Mustache`
- `TurboParser::TbeSchema`（兼容别名 `TurboParser::SchemaBE`）
- `TurboParser::DataBind`

其他格式解析器当前作为 `TurboParser::Parser`、Mustache 或 DataBind 的私有静态组成部分。它们可以在
构建树中通过 `TurboParser::*Parser` alias 单独测试，但不构成已安装的公共组件。
第三方 cxml 与 Monocypher 由 TurboParser 私有持有，不向使用者暴露其 target 或生命周期。

TBE schema 与 DataBind 属于运行时层；`tbe_compiler` 只在构建、CI 和代码生成阶段运行，
不会被 DataBind 在运行时调用。DataBind 的公共头文件包含 `turbo_parser.h`，因此
`TurboParser::Parser` 是其 PUBLIC 依赖；schema 与 Mustache 仅为实现或编译器依赖。
依赖 CmdParser、cxml 与 Mustache 的 `junit_to_html` 也归 TurboParser 所有；它是构建树
工具，不构成 TurboUtils 或 TurboParser 的安装时库依赖。
依赖 CSV parser 的 SQLite VDBE benchmark 随 CSV 模块维护，避免 TurboUtils 测试目标
携带 parser 链接项。

## 规划中的 CBind / CFlow 集成边界

CBind 属于 TurboUtils 基础数据语义层，而不是 TurboParser 产品：

```text
TurboUtils::CBind
    -> TurboUtils::CMeta
    -> TurboUtils::CSerde
```

TurboParser 不拥有或导出 `TurboParser::CBind`。需要 generic binding 的应用直接链接
`TurboUtils::CBind`；TurboParser 后续的 format adapter 也作为它的 consumer。

Parser direct binding 的依赖方向固定为：

```text
TurboParser concrete parser/query
    -> format-specific projection adapter
    -> TurboUtils::CSerde / TurboUtils::CBind
    -> native C object
```

JSON/YAML/XML/CSV 的语法、query、attribute/header/alias 等 policy 继续由 TurboParser 持有，
不得下沉到 CMeta/CSerde/CBind core。

CSerde v1 的 CBind substrate 是 pull `cserde_reader`，而部分 TurboParser parser 是 SAX/event push。
TurboParser 不得通过 include TurboUtils private header 或调用 private CBind machine 来跨 repo 集成。
若后续 direct SAX binding 要求 no-DOM、bounded 且无 unbounded token queue，应先为 CBind 单独设计
public incremental decoder contract；该 ABI 不属于当前 CBind D2。

CFlow composition 只从完整 semantic/native value 开始：

```text
parser source
    -> complete semantic value
    -> TurboUtils::CBind
    -> native object T
    -> TurboUtils::CFlow Stream<T>
```

raw `cserde_token` 是结构化 transport，不作为允许任意 `filter/map` 的业务 `Stream<T>` 暴露，
避免业务 operator 删除 `MAP_END`、field key 或破坏 key/value pairing。

DataBind/TbeTyped 可在后续 migration 中把 generic semantic/native binding 收敛到
CMeta/CBind；TBE-specific wire offset、endianness、presence bitmap、fixed block、group、var-data
继续留在 TurboParser/TBE。

详细 integration design：

```text
docs/superpowers/specs/2026-08-23-cbind-parser-cflow-design.md
```

## 候选方案

1. 继续把解析器留在 TurboUtils：无需迁移，但基础工具包必须携带全部格式解析器，
   也无法独立演进查询运行时。
2. TurboParser 复制 TurboUtils 基础实现：构建独立，但产生错误码、容器和正则的
   双重事实源，修复和 ABI 容易分叉。
3. TurboParser 单向依赖 TurboUtils：需要独立 package/export 配置，但保留基础能力
   的唯一事实源。本仓库采用此方案。

## 接口、所有权与错误语义

本次拆分不改变 `turbo_parser.h` 的函数、数据格式、对象所有权或错误码。解析结果仍由
对应的 `turbo_free_*`/`*_free` 接口释放。QueryVM 程序和 diagnostic 的生命周期也保持
不变。TurboUtils 类型只在已有公共契约中出现，不将其内部源码头文件暴露给使用者。

JSONPath contains 扫描通过已安装的 `turbo_simd_scan.h` 调用
`turbo_scan_mem()`；TurboParser 不再包含 TurboUtils 私有 `re_scan.h`。输入仍是显式
长度的只读字节视图，返回值仍只表达是否找到子串。

## 构建与发布

配置阶段执行 `find_package(TurboUtils CONFIG REQUIRED)`，缺少
`TurboUtils::Core` 时立即失败。启用测试时还要求 `TurboUtils::TinyTest`。安装包通过
`TurboParserConfig.cmake` 调用 `find_dependency(TurboUtils CONFIG)`，因此消费方只需把
两个安装前缀加入 `CMAKE_PREFIX_PATH`。

Windows 测试进程通过 preset 的 `PATH` 查找 TurboUtils DLL；构建系统不复制外部 DLL。
Linux preset 对应设置 `LD_LIBRARY_PATH`。

## 权衡

- 性能：公共 SIMD 扫描 API 保留原有向量化路径；未增加每次匹配的虚调用或分配。
- 复杂度：新增一个 CMake package 边界，但移除了缺失子目录和重复 vendor target。
- 可维护性：TurboUtils API 是基础能力的唯一事实源；解析器 target 使用独立命名空间。
- 兼容性：C API 保持稳定；CMake 使用者需把 Parser、Cron、Mustache、TBE 和 DataBind
  target 从 `TurboUtils::*` 迁移到 `TurboParser::*`，并改为
  `find_package(TurboParser CONFIG REQUIRED)`。

## 迁移与回滚

迁移顺序为：先安装 TurboUtils，再配置、构建和安装 TurboParser，最后迁移消费方的
CMake target。发布前至少运行全量 CTest，并用 staging prefix 检查导出 target。

若独立发布出现阻断，可回滚消费方的 CMake target 到已发布的
`TurboUtils::Parser`；数据和 C API 无需迁移。不得通过重新引入 TurboUtils 源码目录或
复制 DLL 的方式绕过 package 依赖。
