#pragma once
#include <dawn/webgpu.h>

namespace hyp::wgpu
{
    // 挂到 WGPUDeviceDescriptor 上的磁盘持久化 Dawn Blob Cache: Dawn 内部会用它缓存 Tint
    // 翻译结果以及各后端(HLSL/MSL/SPIR-V)的原生 PSO 编译产物, 落盘到可执行文件所在目录下的
    // Cache/DawnCache,
    // 纯粹是编译加速用的派生数据, 删掉整个目录不影响正确性, 只是下次冷启动又要重新走一遍
    // Tint + 原生驱动编译
    WGPUDawnCacheDeviceDescriptor createDawnCacheDeviceDescriptor();
}
