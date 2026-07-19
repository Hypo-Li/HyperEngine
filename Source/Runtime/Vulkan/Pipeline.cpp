#include "Pipeline.h"
#include "BindGroup.h"
#include "Device.h"
#include "RenderPassCache.h"
#include "PipelineCache.h"
#include "ShaderModule.h"
#include "VkFormatUtil.h"

#include <Core/Common/LogSystem.h>

#include <array>
#include <vector>

namespace hyp::vk
{
    PipelineLayout::PipelineLayout(const Device& device, const rhi::PipelineLayoutDesc& desc)
        : rhi::PipelineLayout(device)
    {
        std::array<VkDescriptorSetLayout, 4> setLayouts = { VK_NULL_HANDLE };
        uint32_t bindGroupLayoutCount = 0;
        for (uint32_t i = 0; i < 4; ++i)
        {
            if (desc.bindGroupLayouts[i])
            {
                setLayouts[i] = static_cast<const BindGroupLayout*>(desc.bindGroupLayouts[i])->getHandle();
                bindGroupLayoutCount++;
            }
        }

        VkPushConstantRange pushConstantRange = {};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_ALL;
        pushConstantRange.offset = 0;
        pushConstantRange.size = rhi::kImmediateDataSize;

        VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = bindGroupLayoutCount;
        pipelineLayoutInfo.pSetLayouts = setLayouts.data();
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
        vkCreatePipelineLayout(device.getHandle(), &pipelineLayoutInfo, nullptr, &mPipelineLayout);
    }

    PipelineLayout::~PipelineLayout()
    {
        if (mPipelineLayout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(getVkDevice().getHandle(), mPipelineLayout, nullptr);
        }
    }

    static VkShaderStageFlagBits convertShaderStage(rhi::ShaderStage stage) noexcept
    {
        switch (stage)
        {
        case rhi::ShaderStage::Vertex:   return VK_SHADER_STAGE_VERTEX_BIT;
        case rhi::ShaderStage::Fragment: return VK_SHADER_STAGE_FRAGMENT_BIT;
        case rhi::ShaderStage::Compute:  return VK_SHADER_STAGE_COMPUTE_BIT;
        default:                         return VK_SHADER_STAGE_ALL;
        }
    }

    static VkFormat convertVertexFormat(rhi::VertexFormat format) noexcept
    {
        switch (format)
        {
        case rhi::VertexFormat::Float16:    return VK_FORMAT_R16_SFLOAT;
        case rhi::VertexFormat::Float16x2:  return VK_FORMAT_R16G16_SFLOAT;
        case rhi::VertexFormat::Float16x3:  return VK_FORMAT_R16G16B16_SFLOAT;
        case rhi::VertexFormat::Float16x4:  return VK_FORMAT_R16G16B16A16_SFLOAT;
        case rhi::VertexFormat::Float32:    return VK_FORMAT_R32_SFLOAT;
        case rhi::VertexFormat::Float32x2:  return VK_FORMAT_R32G32_SFLOAT;
        case rhi::VertexFormat::Float32x3:  return VK_FORMAT_R32G32B32_SFLOAT;
        case rhi::VertexFormat::Float32x4:  return VK_FORMAT_R32G32B32A32_SFLOAT;
        case rhi::VertexFormat::Uint32:     return VK_FORMAT_R32_UINT;
        case rhi::VertexFormat::Uint32x2:   return VK_FORMAT_R32G32_UINT;
        case rhi::VertexFormat::Uint32x3:   return VK_FORMAT_R32G32B32_UINT;
        case rhi::VertexFormat::Uint32x4:   return VK_FORMAT_R32G32B32A32_UINT;
        case rhi::VertexFormat::Sint32:     return VK_FORMAT_R32_SINT;
        case rhi::VertexFormat::Sint32x2:   return VK_FORMAT_R32G32_SINT;
        case rhi::VertexFormat::Sint32x3:   return VK_FORMAT_R32G32B32_SINT;
        case rhi::VertexFormat::Sint32x4:   return VK_FORMAT_R32G32B32A32_SINT;
        default:                            return VK_FORMAT_UNDEFINED;
        }
    }

    static VkVertexInputRate convertVertexInputRate(rhi::VertexInputRate rate) noexcept
    {
        switch (rate)
        {
        case rhi::VertexInputRate::Instance: return VK_VERTEX_INPUT_RATE_INSTANCE;
        default:                             return VK_VERTEX_INPUT_RATE_VERTEX;
        }
    }

    static VkPrimitiveTopology convertPrimitiveTopology(rhi::PrimitiveTopology topology) noexcept
    {
        switch (topology)
        {
        case rhi::PrimitiveTopology::Point_List:     return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case rhi::PrimitiveTopology::Line_List:      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case rhi::PrimitiveTopology::Line_Strip:     return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case rhi::PrimitiveTopology::Triangle_List:  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case rhi::PrimitiveTopology::Triangle_Strip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        default:                                     return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        }
    }

    static VkPolygonMode convertPolygonMode(rhi::PolygonMode mode) noexcept
    {
        switch (mode)
        {
        case rhi::PolygonMode::Line:  return VK_POLYGON_MODE_LINE;
        case rhi::PolygonMode::Point: return VK_POLYGON_MODE_POINT;
        default:                      return VK_POLYGON_MODE_FILL;
        }
    }

    static VkCullModeFlags convertCullMode(rhi::CullMode mode) noexcept
    {
        switch (mode)
        {
        case rhi::CullMode::Front:          return VK_CULL_MODE_FRONT_BIT;
        case rhi::CullMode::Back:           return VK_CULL_MODE_BACK_BIT;
        case rhi::CullMode::Front_And_Back: return VK_CULL_MODE_FRONT_AND_BACK;
        default:                            return VK_CULL_MODE_NONE;
        }
    }

    static VkFrontFace convertFrontFace(rhi::FrontFace face) noexcept
    {
        switch (face)
        {
        case rhi::FrontFace::Clockwise:         return VK_FRONT_FACE_CLOCKWISE;
        default:                                return VK_FRONT_FACE_COUNTER_CLOCKWISE;
        }
    }

    static VkCompareOp convertCompareOp(rhi::CompareOp op) noexcept
    {
        switch (op)
        {
        case rhi::CompareOp::Never:          return VK_COMPARE_OP_NEVER;
        case rhi::CompareOp::Less:           return VK_COMPARE_OP_LESS;
        case rhi::CompareOp::Equal:          return VK_COMPARE_OP_EQUAL;
        case rhi::CompareOp::Less_Equal:     return VK_COMPARE_OP_LESS_OR_EQUAL;
        case rhi::CompareOp::Greater:        return VK_COMPARE_OP_GREATER;
        case rhi::CompareOp::Not_Equal:      return VK_COMPARE_OP_NOT_EQUAL;
        case rhi::CompareOp::Greater_Equal:  return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case rhi::CompareOp::Always:         return VK_COMPARE_OP_ALWAYS;
        default:                             return VK_COMPARE_OP_ALWAYS;
        }
    }

    static VkStencilOp convertStencilOp(rhi::StencilOp op) noexcept
    {
        switch (op)
        {
        case rhi::StencilOp::Keep:                return VK_STENCIL_OP_KEEP;
        case rhi::StencilOp::Zero:                return VK_STENCIL_OP_ZERO;
        case rhi::StencilOp::Replace:             return VK_STENCIL_OP_REPLACE;
        case rhi::StencilOp::Increment_And_Clamp: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        case rhi::StencilOp::Decrement_And_Clamp: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
        case rhi::StencilOp::Invert:              return VK_STENCIL_OP_INVERT;
        case rhi::StencilOp::Increment_And_Wrap:  return VK_STENCIL_OP_INCREMENT_AND_WRAP;
        case rhi::StencilOp::Decrement_And_Wrap:  return VK_STENCIL_OP_DECREMENT_AND_WRAP;
        default:                                  return VK_STENCIL_OP_KEEP;
        }
    }

    static VkBlendFactor convertBlendFactor(rhi::BlendFactor factor) noexcept
    {
        switch (factor)
        {
        case rhi::BlendFactor::Zero:                     return VK_BLEND_FACTOR_ZERO;
        case rhi::BlendFactor::One:                      return VK_BLEND_FACTOR_ONE;
        case rhi::BlendFactor::Src_Color:                return VK_BLEND_FACTOR_SRC_COLOR;
        case rhi::BlendFactor::One_Minus_Src_Color:      return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case rhi::BlendFactor::Dst_Color:                return VK_BLEND_FACTOR_DST_COLOR;
        case rhi::BlendFactor::One_Minus_Dst_Color:      return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case rhi::BlendFactor::Src_Alpha:                return VK_BLEND_FACTOR_SRC_ALPHA;
        case rhi::BlendFactor::One_Minus_Src_Alpha:      return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case rhi::BlendFactor::Dst_Alpha:                return VK_BLEND_FACTOR_DST_ALPHA;
        case rhi::BlendFactor::One_Minus_Dst_Alpha:      return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case rhi::BlendFactor::Constant_Color:           return VK_BLEND_FACTOR_CONSTANT_COLOR;
        case rhi::BlendFactor::One_Minus_Constant_Color: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
        case rhi::BlendFactor::Constant_Alpha:           return VK_BLEND_FACTOR_CONSTANT_ALPHA;
        case rhi::BlendFactor::One_Minus_Constant_Alpha: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
        case rhi::BlendFactor::Src_Alpha_Saturate:       return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
        case rhi::BlendFactor::Src1_Color:               return VK_BLEND_FACTOR_SRC1_COLOR;
        case rhi::BlendFactor::One_Minus_Src1_Color:     return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
        case rhi::BlendFactor::Src1_Alpha:               return VK_BLEND_FACTOR_SRC1_ALPHA;
        default:                                         return VK_BLEND_FACTOR_ONE;
        }
    }

    static VkBlendOp convertBlendOp(rhi::BlendOp op) noexcept
    {
        switch (op)
        {
        case rhi::BlendOp::Subtract:         return VK_BLEND_OP_SUBTRACT;
        case rhi::BlendOp::Reverse_Subtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
        case rhi::BlendOp::Min:              return VK_BLEND_OP_MIN;
        case rhi::BlendOp::Max:              return VK_BLEND_OP_MAX;
        default:                             return VK_BLEND_OP_ADD;
        }
    }

    static VkColorComponentFlags convertColorComponentFlags(uint8_t mask) noexcept
    {
        VkColorComponentFlags flags = 0;
        if (mask & 0x1) flags |= VK_COLOR_COMPONENT_R_BIT;
        if (mask & 0x2) flags |= VK_COLOR_COMPONENT_G_BIT;
        if (mask & 0x4) flags |= VK_COLOR_COMPONENT_B_BIT;
        if (mask & 0x8) flags |= VK_COLOR_COMPONENT_A_BIT;
        return flags;
    }

    Pipeline::Pipeline(const Device& device, const rhi::GraphicsPipelineDesc& desc)
        : rhi::Pipeline(device, *desc.pipelineLayout, rhi::PipelineType::Graphics)
    {
        VkPipelineLayout pipelineLayout = static_cast<const PipelineLayout*>(desc.pipelineLayout)->getHandle();

        std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
        shaderStages.reserve(desc.shaderStages.size());

        for (uint32_t i = 0; i < desc.shaderStages.size(); ++i)
        {
            const rhi::ShaderModule* module = desc.shaderStages[i];
            if (!module)
                continue;

            rhi::ShaderStage stage = module->getStage();
            if (stage == rhi::ShaderStage::Compute)
                continue;

            VkPipelineShaderStageCreateInfo stageInfo = {};
            stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stageInfo.stage = convertShaderStage(stage);
            stageInfo.module = static_cast<const ShaderModule*>(module)->getHandle();
            stageInfo.pName = "main";
            shaderStages.push_back(stageInfo);
        }

        std::vector<VkVertexInputBindingDescription> vertexBindings;
        vertexBindings.reserve(desc.vertexInputState.bindings.size());
        for (const auto& binding : desc.vertexInputState.bindings)
        {
            VkVertexInputBindingDescription vkBinding = {};
            vkBinding.binding = binding.binding;
            vkBinding.stride = binding.stride;
            vkBinding.inputRate = convertVertexInputRate(binding.inputRate);
            vertexBindings.push_back(vkBinding);
        }

        std::vector<VkVertexInputAttributeDescription> vertexAttributes;
        vertexAttributes.reserve(desc.vertexInputState.attributes.size());
        for (const auto& attr : desc.vertexInputState.attributes)
        {
            VkVertexInputAttributeDescription vkAttr = {};
            vkAttr.location = attr.location;
            vkAttr.binding = attr.binding;
            vkAttr.format = convertVertexFormat(attr.format);
            vkAttr.offset = attr.offset;
            vertexAttributes.push_back(vkAttr);
        }

        VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexBindings.size());
        vertexInputInfo.pVertexBindingDescriptions = vertexBindings.data();
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size());
        vertexInputInfo.pVertexAttributeDescriptions = vertexAttributes.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = convertPrimitiveTopology(desc.inputAssemblyState.topology);
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo viewportState = {};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer = {};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = desc.rasterizationState.depthClampEnable ? VK_TRUE : VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = convertPolygonMode(desc.rasterizationState.polygonMode);
        rasterizer.cullMode = convertCullMode(desc.rasterizationState.cullMode);
        rasterizer.frontFace = convertFrontFace(desc.rasterizationState.frontFace);
        rasterizer.depthBiasEnable = desc.rasterizationState.depthBiasEnable ? VK_TRUE : VK_FALSE;
        rasterizer.depthBiasConstantFactor = desc.rasterizationState.depthBiasConstantFactor;
        rasterizer.depthBiasClamp = desc.rasterizationState.depthBiasClamp;
        rasterizer.depthBiasSlopeFactor = desc.rasterizationState.depthBiasSlopeFactor;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisampling = {};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = static_cast<VkSampleCountFlagBits>(desc.multisampleState.sampleCount);
        multisampling.alphaToCoverageEnable = desc.multisampleState.alphaToCoverageEnable ? VK_TRUE : VK_FALSE;

        VkPipelineDepthStencilStateCreateInfo depthStencil = {};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = desc.depthStencilState.depthTestEnable ? VK_TRUE : VK_FALSE;
        depthStencil.depthWriteEnable = desc.depthStencilState.depthWriteEnable ? VK_TRUE : VK_FALSE;
        depthStencil.depthCompareOp = convertCompareOp(desc.depthStencilState.depthCompareOp);
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = desc.depthStencilState.stencilTestEnable ? VK_TRUE : VK_FALSE;
        depthStencil.front.failOp = convertStencilOp(desc.depthStencilState.front.failOp);
        depthStencil.front.passOp = convertStencilOp(desc.depthStencilState.front.passOp);
        depthStencil.front.depthFailOp = convertStencilOp(desc.depthStencilState.front.depthFailOp);
        depthStencil.front.compareOp = convertCompareOp(desc.depthStencilState.front.compareOp);
        depthStencil.front.compareMask = desc.depthStencilState.front.compareMask;
        depthStencil.front.writeMask = desc.depthStencilState.front.writeMask;
        depthStencil.front.reference = 0;
        depthStencil.back.failOp = convertStencilOp(desc.depthStencilState.back.failOp);
        depthStencil.back.passOp = convertStencilOp(desc.depthStencilState.back.passOp);
        depthStencil.back.depthFailOp = convertStencilOp(desc.depthStencilState.back.depthFailOp);
        depthStencil.back.compareOp = convertCompareOp(desc.depthStencilState.back.compareOp);
        depthStencil.back.compareMask = desc.depthStencilState.back.compareMask;
        depthStencil.back.writeMask = desc.depthStencilState.back.writeMask;
        depthStencil.back.reference = 0;

        const uint32_t colorAttachmentCount = static_cast<uint32_t>(desc.colorFormats.size());

        std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments(colorAttachmentCount);
        for (uint32_t i = 0; i < colorAttachmentCount; ++i)
        {
            const rhi::ColorBlendAttachmentState* src = nullptr;
            if (i < desc.colorBlendState.attachments.size())
                src = &desc.colorBlendState.attachments[i];

            VkPipelineColorBlendAttachmentState& dst = colorBlendAttachments[i];
            dst = {};
            dst.blendEnable = src && src->blendEnable ? VK_TRUE : VK_FALSE;
            dst.srcColorBlendFactor = convertBlendFactor(src ? src->srcColorBlendFactor : rhi::BlendFactor::One);
            dst.dstColorBlendFactor = convertBlendFactor(src ? src->dstColorBlendFactor : rhi::BlendFactor::Zero);
            dst.colorBlendOp = convertBlendOp(src ? src->colorBlendOp : rhi::BlendOp::Add);
            dst.srcAlphaBlendFactor = convertBlendFactor(src ? src->srcAlphaBlendFactor : rhi::BlendFactor::One);
            dst.dstAlphaBlendFactor = convertBlendFactor(src ? src->dstAlphaBlendFactor : rhi::BlendFactor::Zero);
            dst.alphaBlendOp = convertBlendOp(src ? src->alphaBlendOp : rhi::BlendOp::Add);
            dst.colorWriteMask = convertColorComponentFlags(src ? src->colorWriteMask : 0xFF);
        }

        VkPipelineColorBlendStateCreateInfo colorBlending = {};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = colorAttachmentCount;
        colorBlending.pAttachments = colorBlendAttachments.data();

        std::array<VkDynamicState, 4> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_BLEND_CONSTANTS,
            VK_DYNAMIC_STATE_STENCIL_REFERENCE,
        };

        VkPipelineDynamicStateCreateInfo dynamicState = {};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        std::vector<VkFormat> colorFormats(colorAttachmentCount);
        for (uint32_t i = 0; i < colorAttachmentCount; ++i)
        {
            colorFormats[i] = convertToVkFormat(desc.colorFormats[i]);
        }

        VkFormat depthStencilFormat = convertToVkFormat(desc.depthStencilFormat);
        VkFormat depthFormat = VK_FORMAT_UNDEFINED;
        VkFormat stencilFormat = VK_FORMAT_UNDEFINED;

        if (depthStencilFormat != VK_FORMAT_UNDEFINED)
        {
            switch (depthStencilFormat)
            {
            case VK_FORMAT_D16_UNORM:
            case VK_FORMAT_D32_SFLOAT:
                depthFormat = depthStencilFormat;
                break;
            case VK_FORMAT_S8_UINT:
                stencilFormat = depthStencilFormat;
                break;
            default:
                depthFormat = depthStencilFormat;
                stencilFormat = depthStencilFormat;
                break;
            }
        }

        VkPipelineRenderingCreateInfo renderingInfo = {};
        VkRenderPass renderPass = VK_NULL_HANDLE;

        if (device.getVkFeatures().dynamicRendering)
        {
            renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
            renderingInfo.viewMask = 0;
            renderingInfo.colorAttachmentCount = colorAttachmentCount;
            renderingInfo.pColorAttachmentFormats = colorFormats.data();
            renderingInfo.depthAttachmentFormat = depthFormat;
            renderingInfo.stencilAttachmentFormat = stencilFormat;
        }
        else
        {
            renderPass = device.getRenderPassCache().getOrCreateRenderPass(
                colorFormats,
                depthStencilFormat,
                depthFormat,
                stencilFormat,
                static_cast<VkSampleCountFlagBits>(desc.multisampleState.sampleCount)
            );
        }

        VkGraphicsPipelineCreateInfo pipelineInfo = {};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = device.getVkFeatures().dynamicRendering ? &renderingInfo : nullptr;
        pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineInfo.pStages = shaderStages.data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
        pipelineInfo.basePipelineIndex = -1;

        if (vkCreateGraphicsPipelines(device.getHandle(), device.getPipelineCache().getHandle(), 1, &pipelineInfo, nullptr, &mPipeline) != VK_SUCCESS)
        {
            LOG_ERROR("Failed to create Vulkan graphics pipeline");
        }
    }

    Pipeline::Pipeline(const Device& device, const rhi::ComputePipelineDesc& desc)
        : rhi::Pipeline(device, *desc.pipelineLayout, rhi::PipelineType::Compute)
    {
        VkPipelineLayout pipelineLayout = static_cast<const PipelineLayout*>(desc.pipelineLayout)->getHandle();

        VkComputePipelineCreateInfo pipelineInfo = {};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.layout = pipelineLayout;

        const ShaderModule* shader = static_cast<const ShaderModule*>(desc.shaderModule);
        VkPipelineShaderStageCreateInfo stageInfo = {};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = shader->getHandle();
        stageInfo.pName = "main";
        pipelineInfo.stage = stageInfo;

        if (vkCreateComputePipelines(device.getHandle(), device.getPipelineCache().getHandle(), 1, &pipelineInfo, nullptr, &mPipeline) != VK_SUCCESS)
        {
            LOG_ERROR("Failed to create Vulkan compute pipeline");
        }
    }

    Pipeline::~Pipeline()
    {
        if (mPipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(getVkDevice().getHandle(), mPipeline, nullptr);
        }
    }

    VkPipelineBindPoint Pipeline::getBindPoint() const noexcept
    {
        return getType() == rhi::PipelineType::Compute
            ? VK_PIPELINE_BIND_POINT_COMPUTE
            : VK_PIPELINE_BIND_POINT_GRAPHICS;
    }
}
