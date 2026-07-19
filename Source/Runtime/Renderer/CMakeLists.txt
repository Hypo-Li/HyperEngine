add_smart_library(Renderer)

target_link_libraries (Renderer PUBLIC Core RenderBase vk_radix_sort::vk_radix_sort)

# Renderer 是 PassShaders 预编译出来的 <Name>.generated.h(GSplat/Culling/Atmosphere 等 pass shader
# 的 SPIRV+WGSL 字节码)的唯一使用方,add_dependencies 保证编译 Renderer 之前 PassShaders 已经把这些
# 头文件生成好,target_include_directories 让 Renderer 的源文件能 #include 到它们
add_dependencies(Renderer PassShaders)
target_include_directories(Renderer PUBLIC "${CMAKE_BINARY_DIR}/GeneratedShaders/$<CONFIG>")
