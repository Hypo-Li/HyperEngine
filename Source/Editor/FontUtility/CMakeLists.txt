add_smart_library(FontUtility)

target_link_libraries(FontUtility PUBLIC Engine)

find_package(Freetype REQUIRED)
find_package(harfbuzz CONFIG REQUIRED)
find_package(msdfgen CONFIG REQUIRED)
target_link_libraries(FontUtility PRIVATE Freetype::Freetype harfbuzz::harfbuzz msdfgen::msdfgen-full)

find_path(STB_INCLUDE_DIRS "stb_rect_pack.h")
target_include_directories(FontUtility PRIVATE ${STB_INCLUDE_DIRS})
