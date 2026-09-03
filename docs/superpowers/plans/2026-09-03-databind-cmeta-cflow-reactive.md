# DataBind CMeta / CFlow / Reactive Adapter Implementation Plan

> **For agentic workers:** Execute each task in order with a RED-GREEN-REFACTOR checkpoint; do not parallelize changes that share the adapter ABI.

**Goal:** 在不改变现有 DataBind ABI/行为的前提下，增加 CMeta range、CFlow stream 和 Reactive publisher 适配，并修复版本化 stream config 的越界字段访问。

**Architecture:** 保持 `TurboParser::DataBind` 为格式与不可变值核心；新增 `DataBindCMeta` 和 `DataBindCFlow` 两个可选薄适配库。CMeta Schema/Replay 生成三种借用引用类型的 descriptor，CFlow 复用既有 range factories，不新增队列、调度器或自定义背压状态机。

**Tech Stack:** C11、TurboParser DataBind、Rocida CMeta/CFlow/Reactive、CMake Presets、TinyTest。

**Spec:** `docs/architecture/databind-cmeta-cflow-adapters.md`

## Global Constraints

- 不修改既有公开 enum 数值、DataBind 数据格式、核心 target 依赖或用户可见解析行为。
- 每个行为改动先加入失败测试，并确认失败原因与目标一致。
- 新适配值全部为 borrowed view；根 `DataBindValue` 的生命周期必须覆盖全部消费过程。
- range/publisher 零缓冲、有限长度、保持 encounter/schema order；不得以无界分配代替背压。
- `.codegraph/` 与 build tree 不提交。

## Task 1: 修复版本化 stream config 边界

**Files:**

- Modify: `tbe/data_bind/test_data_bind_public_api.c`
- Modify: `tbe/data_bind/data_bind.c`

- [x] 增加截断 `DataBindStreamConfig.size` 测试；把超出声明 size 的 `out_value` 设为可观察 sentinel。
- [x] 运行 focused test，确认当前实现错误地改写 sentinel。
- [x] 把最小 size 校验移到所有可选字段访问之前，并保持既有错误码。
- [x] 重跑 focused test 与既有 stream API 测试。

## Task 2: CMeta range 适配

**Files:**

- Create: `tbe/data_bind/data_bind_cmeta.h`
- Create: `tbe/data_bind/data_bind_cmeta.c`
- Create: `tbe/data_bind/test_data_bind_cmeta.c`
- Modify: `tbe/data_bind/CMakeLists.txt`

- [x] 先写 LIST/SET、OBJECT、MAP 的 range 测试，断言 element descriptor、size、顺序、`VALUE_AND_DONE`/`DONE` 和错误时零输出。
- [x] 构建确认测试因适配 API 缺失而失败。
- [x] 定义三个 borrowed ref 类型和显式 range-kind enum；用 CMeta Schema/Replay 生成 identity/descriptor/getter。
- [x] 实现 allocation-free cursor；仅调用 DataBind 公共只读 accessor，不穿透内部结构。
- [x] 重跑 focused test，并编译 C++ 头文件消费用例。

## Task 3: CFlow Stream 与 Reactive Publisher 适配

**Files:**

- Create: `tbe/data_bind/data_bind_cflow.h`
- Create: `tbe/data_bind/data_bind_cflow.c`
- Create: `tbe/data_bind/test_data_bind_cflow.c`
- Modify: `tbe/data_bind/CMakeLists.txt`

- [x] 先写 source-only stream 与 publisher 测试；publisher 必须验证 request(1) 只产生一项、下一次 demand 产生末项和 terminal。
- [x] 构建确认测试因 CFlow 适配 API 缺失而失败。
- [x] 复用 `data_bind_cmeta_range_init` 与 CFlow 官方 factory；factory 失败统一转成 DataBind runtime error。
- [x] 验证 cancel/close 不释放 DataBind owner，且 owner 在关闭后仍可读。
- [x] 重跑 CFlow/Reactive focused test。

## Task 4: 构建、安装与回归

**Files:**

- Modify: `tbe/data_bind/CMakeLists.txt`
- Modify: `tbe/data_bind/README.md`

- [x] 增加两个可选 shared target、namespaced alias、正确的 PUBLIC/PRIVATE 链接边界与安装 header。
- [x] 文档化 target、示例、所有权、失效点、线程和错误契约。
- [x] 用 win-release-user preset 构建新旧 DataBind targets，先跑新测试，再跑全部 DataBind CTest。
- [x] 执行安装构建并验证安装目录中头文件、DLL/import library 和传递依赖。
- [x] 运行 `codegraph sync .`/`affected`、`git diff --check` 和 `git status --short`，记录残余风险。
