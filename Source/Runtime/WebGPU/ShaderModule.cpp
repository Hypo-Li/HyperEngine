#include "ShaderModule.h"

#include <Core/Common/LogSystem.h>
#include <cassert>

namespace hyp::wgpu
{
    ShaderModule::ShaderModule(const Device& device, rhi::ShaderStage stage, std::vector<uint8_t>&& code, rhi::ShaderCodeFormat codeFormat)
        : rhi::ShaderModule(device, stage, std::move(code), codeFormat)
    {
        WGPUShaderModuleDescriptor desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
        desc.label = WGPUStringView{ nullptr, 0 };

        if (codeFormat == rhi::ShaderCodeFormat::Spirv)
        {
            WGPUShaderSourceSPIRV spirv = WGPU_SHADER_SOURCE_SPIRV_INIT;
            spirv.codeSize = static_cast<uint32_t>(getCode().size()) / sizeof(uint32_t);
            spirv.code = reinterpret_cast<const uint32_t*>(getCode().data());
            desc.nextInChain = &spirv.chain;
            mShaderModule = wgpuDeviceCreateShaderModule(device.getHandle(), &desc);
        }
        else if (codeFormat == rhi::ShaderCodeFormat::Wgsl)
        {
            assert(!getCode().empty() && getCode().back() == '\0' && "WGSL code must be null-terminated");
            std::string_view preview = std::string_view{ (const char*)(getCode().data()), getCode().size() - 1};

            WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
            wgsl.code = WGPUStringView{ reinterpret_cast<const char*>(getCode().data()), getCode().size() - 1 };
            desc.nextInChain = &wgsl.chain;
            mShaderModule = wgpuDeviceCreateShaderModule(device.getHandle(), &desc);
        }
        else
        {
            LOG_ERROR("Unsupported WebGPU shader code format");
        }
    }

    ShaderModule::~ShaderModule()
    {
        if (mShaderModule)
        {
            wgpuShaderModuleRelease(mShaderModule);
            mShaderModule = nullptr;
        }
    }

    void ShaderModule::setDebugName(HString name)
    {
        rhi::ShaderModule::setDebugName(name);
        WGPUStringView label = WGPUStringView{ getDebugName().toCString(), WGPU_STRLEN };
        wgpuShaderModuleSetLabel(mShaderModule, label);
    }
}
