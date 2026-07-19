#pragma once
#include "Device.h"

#include <RHI/ShaderModule.h>

namespace hyp::wgpu
{
    class ShaderModule : public rhi::ShaderModule
    {
    public:
        ShaderModule(const Device& device, rhi::ShaderStage stage, std::vector<uint8_t>&& code, rhi::ShaderCodeFormat codeFormat);

        ~ShaderModule();

        const Device& getWgpuDevice() const noexcept
        {
            return static_cast<const Device&>(getDevice());
        }

        WGPUShaderModule getHandle() const noexcept
        {
            return mShaderModule;
        }

        void setDebugName(HString name) override;

    private:
        WGPUShaderModule mShaderModule = nullptr;
    };
}
