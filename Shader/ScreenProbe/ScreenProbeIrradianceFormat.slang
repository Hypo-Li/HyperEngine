// 跟 Source/Runtime/Renderer/RenderPass/ScreenProbeConstants.h 里的同名常量必须保持一致。
// 绑定表由 slangc 的反射 JSON 生成, 反射的是声明出来的参数布局而不是优化后的 SPIRV, 只有被
// #if 掉的资源声明才不会进表。
#pragma once

#define SCREEN_PROBE_IRRADIANCE_FORMAT_SH3 0
#define SCREEN_PROBE_IRRADIANCE_FORMAT_OCT 1

// 对应 UE r.Lumen.ScreenProbeGather.IrradianceFormat, 它的实际默认值是 0 而不是源码初值 1
#define SCREEN_PROBE_IRRADIANCE_FORMAT SCREEN_PROBE_IRRADIANCE_FORMAT_SH3
