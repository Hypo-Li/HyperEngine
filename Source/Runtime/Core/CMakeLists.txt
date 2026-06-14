find_package(spdlog CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_smart_library(Core)

target_link_libraries(Core PRIVATE
    spdlog::spdlog
    glm::glm-header-only
    nlohmann_json::nlohmann_json
)

target_compile_options(Core PUBLIC /utf-8)

