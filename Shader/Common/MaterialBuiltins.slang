#pragma once
#include <Common/Defines.slang>

#if MATERIAL_PIPELINE == MATERIAL_PIPELINE_RASTER
    #include "MaterialBuiltinsRaster.slang"
#else
    #error "MATERIAL_PIPELINE has no MaterialBuiltins implementation"
#endif

#include "MaterialBuiltinsCommon.slang"
