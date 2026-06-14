add_smart_library(Renderer)

target_link_libraries (Renderer PUBLIC Core RenderBase vk_radix_sort::vk_radix_sort)
