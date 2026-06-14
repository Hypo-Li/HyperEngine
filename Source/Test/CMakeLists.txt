include_directories(
	"${CMAKE_SOURCE_DIR}/ThirdParty"
    "${CMAKE_SOURCE_DIR}/Source/Runtime"
)

add_subdirectory("RenderTest")

set(TEST_TARGETS RenderTest)
set_property(TARGET ${TEST_TARGETS} PROPERTY FOLDER "Test")
