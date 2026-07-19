add_subdirectory("volk")
add_subdirectory("imgui")
add_subdirectory("ispc_texcomp")
add_subdirectory("vk_radix_sort")
add_subdirectory("dds_image")

set(THIRD_PARTY_TARGETS volk imgui vk_radix_sort dds_image)
set_property(TARGET ${THIRD_PARTY_TARGETS} PROPERTY FOLDER "ThirdParty")