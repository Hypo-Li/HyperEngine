#pragma once
#include <RHI/Texture.h>

#include <cstdint>
#include <dawn/webgpu.h>

namespace hyp::wgpu
{
    WGPUTextureFormat convertTextureFormat(rhi::TextureFormat format) noexcept;

    WGPUTextureViewDimension convertTextureViewType(rhi::TextureViewType type) noexcept;
}
