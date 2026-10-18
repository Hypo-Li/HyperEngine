add_smart_library(LanguageIntelligence)

# 诊断改走 slangd 子进程(SlangdBackend, 通过 LspClient)之后, 这里不再直接调用 Slang 编译器
# API, 不需要 RenderBase 那一整条 Vulkan/WebGPU/RHI 依赖链——只需要 Core(LspClient 用
# nlohmann_json 做 JSON-RPC 编解码, 通过 Core PUBLIC 链接传递拿到)和 Platform
# (SlangdBackend 用 Platform::getExecutableDir() 就近发现 slangd.exe)。
# tree-sitter-slang 是 SlangProvider 内部实现细节, 不对外暴露, 用 PRIVATE。
target_link_libraries(LanguageIntelligence PUBLIC Core Platform)
target_link_libraries(LanguageIntelligence PRIVATE tree-sitter-slang)
