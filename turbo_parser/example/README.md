# TurboParser 示例（turbo_parser/example）

本目录提供两个可直接运行的示例：

- `turbo_parser_query_example.c`：使用 C 风格 API (`turbo_parser.h`) 演示 JSONPath / YPath / XPath 查询
- `turbo_parser_query_example.cpp`：使用 C++ 兼容层 (`turbo_parser.hpp`) 演示 `jpath/ypath/xpath` 兼容入口

## 先决条件

- 已按仓库规范配置工具链（CMake + 编译器）
- 已克隆/初始化子模块/依赖（仓库根目录已有 `find_package(TurboUtils)` 的要求）
- 需要时打开示例开关（默认 ON）：
  - `BUILD_EXAMPLES`（顶层 CMake 选项）

## 在仓库根目录构建（推荐）

```bash
cmake -S . -B build -DBUILD_EXAMPLES=ON
cmake --build build --config Release
```

## 运行示例

输出目录为 `${build_dir}/bin`（由顶层 CMake 统一设置）。在默认构建里可执行：

```bash
./build/bin/turbo_parser_query_example_c      # C 示例
./build/bin/turbo_parser_query_example_cpp    # C++ 示例
```

> Windows 下请按本机可执行文件后缀（通常为 `.exe`）。

## 示例说明

### C 示例（`turbo_parser_query_example.c`）

- 演示 `turbo_parse_json` + `turbo_json_path_query`
- 演示 `turbo_parse_yaml` + `turbo_yaml_path_query`
- 演示 `turbo_parse_xml` + `turbo_xml_xpath_text`、`turbo_xml_xpath_query`、`turbo_xml_xpath_node_text`
- 演示 `turbo_*_result_free` / `turbo_*_free` 资源释放

### C++ 示例（`turbo_parser_query_example.cpp`）

- 使用 `turbo_parser.hpp` 的 `Json::parse`
- 使用兼容接口：
  - `turbo_jpath_*`
  - `turbo_ypath_*`
  - `turbo_xpath_*`
- 演示查询结果遍历与对象生命周期释放

## 常见问题

- 如果示例未生成，请确认配置时未手工关闭 `BUILD_EXAMPLES`。
- 如果链接失败，请先确认 `TurboParser::Parser` 库已构建成功（本示例依赖该库）。
- 如果找不到头文件，请确认从仓库顶层构建，避免手工只在子目录里 `cmake`。

## 预期输出（示意）

实际输出与 JSON/YAML/XML 内容格式有关，但应包含：

- JSON 查询结果（数组长度/字段值）
- YAML 查询结果（名称列表）
- XPath 查询结果（节点文本与节点名）

