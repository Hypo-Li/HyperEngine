add_smart_library(RHI)

option(HYP_RHI_COMMAND_RECORDING_IMMEDIATE "Use immediate virtual command recording in RHI CommandList for easier debugging" OFF)
if(HYP_RHI_COMMAND_RECORDING_IMMEDIATE)
    target_compile_definitions(RHI PUBLIC HYP_RHI_COMMAND_RECORDING_IMMEDIATE=1)
endif()

target_link_libraries(RHI PUBLIC
    Core
)
