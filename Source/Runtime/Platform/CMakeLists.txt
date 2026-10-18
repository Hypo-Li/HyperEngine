find_package(glfw3 CONFIG REQUIRED)

add_smart_library(Platform)

target_link_libraries (Platform PUBLIC Core glfw)

if(LINUX)
    find_package(X11 QUIET)
    if(X11_X11_xcb_FOUND AND X11_xcb_FOUND)
        target_compile_definitions(Platform PUBLIC HYP_HAS_XCB)
        target_include_directories(Platform PRIVATE
            "${X11_X11_xcb_INCLUDE_PATH}"
            "${X11_xcb_INCLUDE_PATH}"
        )
        target_link_libraries(Platform PUBLIC
            "${X11_X11_xcb_LIB}"
            "${X11_xcb_LIB}"
        )
    endif()
endif()
