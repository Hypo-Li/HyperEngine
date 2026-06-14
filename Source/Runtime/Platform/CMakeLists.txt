find_package(glfw3 CONFIG REQUIRED)

add_smart_library(Platform)

target_link_libraries (Platform PUBLIC Core glfw)
