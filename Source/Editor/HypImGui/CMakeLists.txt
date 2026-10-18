add_smart_library(HypImGui)

target_link_libraries (HypImGui PUBLIC
    RenderBase
    Platform
    imgui
)
