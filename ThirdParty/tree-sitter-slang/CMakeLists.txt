cmake_minimum_required(VERSION 4.2.3)
project(tree-sitter-slang C)

# tree-sitter-grammars/tree-sitter-slang (MIT) 的生成产物, 原样 vendor 进来, 不手改——
# 该语法在 tree-sitter-hlsl 的基础上扩展出 Slang 特有语法(泛型、interface、generic
# 约束等), 节点种类和字段名与 tree-sitter-hlsl 保持兼容, 生成的状态表同样很大(源文件
# 本身 ~27MB), 编译时间不需要在这里额外处理。
add_library(tree-sitter-slang STATIC
    src/parser.c
    src/scanner.c
)

set_target_properties(tree-sitter-slang PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)

# parser.c/scanner.c 用的是 "tree_sitter/parser.h"(tree-sitter 语法生成产物依赖的内部
# 头文件, 跟 vcpkg 装的运行时公共头 tree_sitter/api.h 是两个不同的文件, 只是恰好同名目录),
# 只有编译这两个文件自己需要, 不对外暴露。
target_include_directories(tree-sitter-slang PRIVATE src)

find_package(unofficial-tree-sitter CONFIG REQUIRED)
target_link_libraries(tree-sitter-slang PUBLIC unofficial::tree-sitter::tree-sitter)
