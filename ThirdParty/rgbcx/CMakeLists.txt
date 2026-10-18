cmake_minimum_required(VERSION 3.20)
project(rgbcx CXX)

# richgel999/bc7enc_rdo(MIT/Public Domain)的 rgbcx.h/rgbcx.cpp, 原样 vendor 进来, 用于 BC1-5 压缩。
add_library(rgbcx STATIC
    rgbcx.cpp
)

set_target_properties(rgbcx PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(rgbcx PUBLIC .)
