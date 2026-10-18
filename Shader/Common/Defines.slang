#pragma once

#define RHI_BACKEND_VULKAN 0
#define RHI_BACKEND_WEBGPU 1

#ifndef RHI_BACKEND
    #define RHI_BACKEND RHI_BACKEND_VULKAN
#endif

#if RHI_BACKEND == RHI_BACKEND_VULKAN
    #define PRIMITIVE_ID SV_PrimitiveID
#elif RHI_BACKEND == RHI_BACKEND_WEBGPU
    // Slang 的 WGSL 后端不认 SV_PrimitiveID, 靠固定 location 占位后由 SlangShaderCompiler 补丁成 @builtin(primitive_index)
    #define PRIMITIVE_ID TEXCOORD15
#else
    #error "Unsupported RHI_BACKEND"
#endif

#define MATERIAL_PIPELINE_RASTER 0
#define MATERIAL_PIPELINE_RESOLVE 1
#define MATERIAL_PIPELINE_RAYTRACE 2

#ifndef MATERIAL_PIPELINE
    #define MATERIAL_PIPELINE MATERIAL_PIPELINE_RASTER
#endif

// 默认值由各模板自己决定: 表面域在 MaterialIO.slang, Sky 在 SkyBackground.slang
#define MATERIAL_DOMAIN_SURFACE 0
#define MATERIAL_DOMAIN_DECAL 1
#define MATERIAL_DOMAIN_VOLUME 2
#define MATERIAL_DOMAIN_POST_PROCESS 3
#define MATERIAL_DOMAIN_USER_INTERFACE 4
#define MATERIAL_DOMAIN_SKY 5

#define BLEND_MODE_OPAQUE 0
#define BLEND_MODE_MASKED 1
#define BLEND_MODE_TRANSLUCENT 2
#define BLEND_MODE_ADDITIVE 3
#define BLEND_MODE_MULTIPLY 4

#ifndef BLEND_MODE
    #define BLEND_MODE BLEND_MODE_OPAQUE
#endif

// #if 看不到 MaterialIO.slang 里的 ShadingModel 枚举(只在编译期可见), 跟 SHADING_MODEL 比较只能用这几个整数宏, 前四个数值需要和枚举保持一致(ShadingModel(SHADING_MODEL) 按数值转换)
#define SHADING_MODEL_UNLIT 0
#define SHADING_MODEL_STANDARD 1
#define SHADING_MODEL_CLEAR_COAT 2
#define SHADING_MODEL_CLOTH 3
#define SHADING_MODEL_FROM_MATERIAL 4

#ifndef SHADING_MODEL
    #define SHADING_MODEL SHADING_MODEL_STANDARD
#endif

#ifndef IS_ANISOTROPIC
    #define IS_ANISOTROPIC 0
#endif

#ifndef ALPHA_CUTOFF
    #define ALPHA_CUTOFF 0.3333
#endif

#ifndef HAS_VPO
    #define HAS_VPO 0
#endif

#ifndef TIME_DEPENDENT_VPO
    #define TIME_DEPENDENT_VPO 0
#endif

#ifndef TANGENT_SPACE_NORMAL
    #define TANGENT_SPACE_NORMAL 1
#endif
