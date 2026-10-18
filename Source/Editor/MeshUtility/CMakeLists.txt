add_smart_library(MeshUtility)

target_link_libraries(MeshUtility PUBLIC Engine)

find_package(embree CONFIG REQUIRED)
target_link_libraries(MeshUtility PRIVATE embree)
