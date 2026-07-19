#include "ShaderModule.h"

namespace hyp::vk
{
    ShaderModule::ShaderModule(const Device& device, rhi::ShaderStage stage, std::vector<uint8_t>&& code, rhi::ShaderCodeFormat codeFormat) :
        rhi::ShaderModule(device, stage, std::move(code), codeFormat)
    {
        if (codeFormat != rhi::ShaderCodeFormat::Spirv)
            return;

        VkShaderModuleCreateInfo shaderModuleInfo = {};
        shaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shaderModuleInfo.codeSize = getCode().size();
        shaderModuleInfo.pCode = reinterpret_cast<const uint32_t*>(getCode().data());
        vkCreateShaderModule(device.getHandle(), &shaderModuleInfo, nullptr, &mShaderModule);
    }

    ShaderModule::~ShaderModule()
    {
        vkDestroyShaderModule(getVkDevice().getHandle(), mShaderModule, nullptr);
    }

    void ShaderModule::setDebugName(HString name)
    {
        rhi::ShaderModule::setDebugName(name);

        if (vkSetDebugUtilsObjectNameEXT)
        {
            VkDebugUtilsObjectNameInfoEXT nameInfo = {};
            nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
            nameInfo.objectType = VK_OBJECT_TYPE_SHADER_MODULE;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(mShaderModule);
            nameInfo.pObjectName = getDebugName().toCString();
            vkSetDebugUtilsObjectNameEXT(getVkDevice().getHandle(), &nameInfo);
        }
    }
}
