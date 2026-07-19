add_smart_library(WebGPU)

find_package(Dawn CONFIG REQUIRED)

target_link_libraries(WebGPU PUBLIC
    RHI
    dawn::webgpu_dawn
)
