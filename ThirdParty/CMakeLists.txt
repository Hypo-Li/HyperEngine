add_subdirectory("volk")
add_subdirectory("imgui")
add_subdirectory("ispc_texcomp")
add_subdirectory("dds_image")
add_subdirectory("tree-sitter-slang")
add_subdirectory("rgbcx")

set(THIRD_PARTY_TARGETS volk imgui dds_image tree-sitter-slang rgbcx)
set_property(TARGET ${THIRD_PARTY_TARGETS} PROPERTY FOLDER "ThirdParty")