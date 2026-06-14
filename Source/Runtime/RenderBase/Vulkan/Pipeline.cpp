#include "Pipeline.h"
#include "DummyRenderPassCache.h"

namespace hyp::vk
{
    ShaderModule::ShaderModule(
        const Device& device,
        std::span<const uint8_t> code
    ) : mDevice(device)
    {
        VkShaderModuleCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

        vkCreateShaderModule(mDevice.getHandle(), &createInfo, nullptr, &mShaderModule);
    }

    ShaderModule::ShaderModule(
        const Device& device,
        std::span<const uint32_t> code
    ) : ShaderModule(device, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(code.data()), code.size() * sizeof(uint32_t)))
    {
    }

    ShaderModule::~ShaderModule()
    {
        vkDestroyShaderModule(mDevice.getHandle(), mShaderModule, nullptr);
    }

    PipelineCache::PipelineCache(const Device& device, std::span<const uint8_t> initialData)
        : mDevice(device)
    {
        VkPipelineCacheCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        createInfo.initialDataSize = initialData.size();
        createInfo.pInitialData = initialData.data();

        vkCreatePipelineCache(mDevice.getHandle(), &createInfo, nullptr, &mPipelineCache);
    }

    PipelineCache::~PipelineCache()
    {
        vkDestroyPipelineCache(mDevice.getHandle(), mPipelineCache, nullptr);
    }

    std::vector<uint8_t> PipelineCache::getData() const
    {
        std::vector<uint8_t> result;
        size_t dataSize;
        vkGetPipelineCacheData(mDevice.getHandle(), mPipelineCache, &dataSize, nullptr);
        result.resize(dataSize);
        vkGetPipelineCacheData(mDevice.getHandle(), mPipelineCache, &dataSize, result.data());
        return result;
    }

    PipelineLayout::PipelineLayout(
        const Device& device,
        std::span<const VkDescriptorSetLayout> setLayouts,
        std::span<const VkPushConstantRange> pushConstantRanges
    ) : mDevice(device)
    {
        VkPipelineLayoutCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        createInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        createInfo.pSetLayouts = setLayouts.data();
        createInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
        createInfo.pPushConstantRanges = pushConstantRanges.data();

        vkCreatePipelineLayout(mDevice.getHandle(), &createInfo, nullptr, &mPipelineLayout);
    }

    PipelineLayout::~PipelineLayout()
    {
        vkDestroyPipelineLayout(mDevice.getHandle(), mPipelineLayout, nullptr);
    }

    Pipeline::Pipeline(const Device& device, const GraphicsCreateInfo& createInfo, VkPipelineCache pipelineCache)
        : mDevice(device), mLayout(createInfo.layout), mBindPoint(VK_PIPELINE_BIND_POINT_GRAPHICS)
    {
        VkGraphicsPipelineCreateInfo pipelineInfo = {};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;

        std::vector<VkPipelineShaderStageCreateInfo> shaderStages(createInfo.shaderStages.size());
        VkShaderStageFlags allStageFlags = 0;
        for (uint32_t i = 0; i < shaderStages.size(); ++i)
        {
            shaderStages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shaderStages[i].stage = createInfo.shaderStages[i].stage;
            shaderStages[i].module = createInfo.shaderStages[i].module;
            shaderStages[i].pName = createInfo.shaderStages[i].name;
            allStageFlags |= shaderStages[i].stage;
        }
        pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineInfo.pStages = shaderStages.data();

        VkPipelineVertexInputStateCreateInfo vertexInputState = {};
        vertexInputState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputState.vertexBindingDescriptionCount = static_cast<uint32_t>(createInfo.vertexInputState.bindings.size());
        vertexInputState.pVertexBindingDescriptions = createInfo.vertexInputState.bindings.data();
        vertexInputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(createInfo.vertexInputState.attributes.size());
        vertexInputState.pVertexAttributeDescriptions = createInfo.vertexInputState.attributes.data();
        pipelineInfo.pVertexInputState = &vertexInputState;

        VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = {};
        inputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssemblyState.topology = createInfo.inputAssemblyState.topology;
        inputAssemblyState.primitiveRestartEnable = createInfo.inputAssemblyState.primitiveRestartEnable;
        pipelineInfo.pInputAssemblyState = &inputAssemblyState;

        VkPipelineTessellationStateCreateInfo tessellationState = {};
        if (allStageFlags & (VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT))
        {
            tessellationState.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
            tessellationState.patchControlPoints = createInfo.tessellationState.patchControlPoints;
            pipelineInfo.pTessellationState = &tessellationState;
        }
        else
        {
            pipelineInfo.pTessellationState = nullptr;
        }

        VkPipelineViewportStateCreateInfo viewportState = {};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = static_cast<uint32_t>(createInfo.viewportState.viewports.size());
        viewportState.pViewports = createInfo.viewportState.viewports.data();
        viewportState.scissorCount = static_cast<uint32_t>(createInfo.viewportState.scissors.size());
        viewportState.pScissors = createInfo.viewportState.scissors.data();
        pipelineInfo.pViewportState = &viewportState;

        VkPipelineRasterizationStateCreateInfo rasterizationState = {};
        rasterizationState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizationState.depthClampEnable = createInfo.rasterizationState.depthClampEnable;
        rasterizationState.rasterizerDiscardEnable = createInfo.rasterizationState.rasterizerDiscardEnable;
        rasterizationState.polygonMode = createInfo.rasterizationState.polygonMode;
        rasterizationState.cullMode = createInfo.rasterizationState.cullMode;
        rasterizationState.frontFace = createInfo.rasterizationState.frontFace;
        rasterizationState.depthBiasEnable = createInfo.rasterizationState.depthBiasEnable;
        rasterizationState.depthBiasConstantFactor = createInfo.rasterizationState.depthBiasConstantFactor;
        rasterizationState.depthBiasClamp = createInfo.rasterizationState.depthBiasClamp;
        rasterizationState.depthBiasSlopeFactor = createInfo.rasterizationState.depthBiasSlopeFactor;
        rasterizationState.lineWidth = createInfo.rasterizationState.lineWidth;
        pipelineInfo.pRasterizationState = &rasterizationState;

        VkPipelineMultisampleStateCreateInfo multisampleState = {};
        multisampleState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampleState.rasterizationSamples = createInfo.multisampleState.rasterizationSamples;
        multisampleState.sampleShadingEnable = createInfo.multisampleState.sampleShadingEnable;
        multisampleState.minSampleShading = createInfo.multisampleState.minSampleShading;
        multisampleState.alphaToCoverageEnable = createInfo.multisampleState.alphaToCoverageEnable;
        multisampleState.alphaToOneEnable = createInfo.multisampleState.alphaToOneEnable;
        pipelineInfo.pMultisampleState = &multisampleState;

        VkPipelineDepthStencilStateCreateInfo depthStencilState = {};
        depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencilState.depthTestEnable = createInfo.depthStencilState.depthTestEnable;
        depthStencilState.depthWriteEnable = createInfo.depthStencilState.depthWriteEnable;
        depthStencilState.depthCompareOp = createInfo.depthStencilState.depthCompareOp;
        depthStencilState.depthBoundsTestEnable = createInfo.depthStencilState.depthBoundsTestEnable;
        depthStencilState.stencilTestEnable = createInfo.depthStencilState.stencilTestEnable;
        depthStencilState.front = createInfo.depthStencilState.front;
        depthStencilState.back = createInfo.depthStencilState.back;
        depthStencilState.minDepthBounds = createInfo.depthStencilState.minDepthBounds;
        depthStencilState.maxDepthBounds = createInfo.depthStencilState.maxDepthBounds;
        pipelineInfo.pDepthStencilState = &depthStencilState;

        VkPipelineColorBlendStateCreateInfo colorBlendState = {};
        colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlendState.logicOpEnable = createInfo.colorBlendState.logicOpEnable;
        colorBlendState.logicOp = createInfo.colorBlendState.logicOp;
        colorBlendState.attachmentCount = static_cast<uint32_t>(createInfo.colorBlendState.attachments.size());
        colorBlendState.pAttachments = createInfo.colorBlendState.attachments.data();
        colorBlendState.blendConstants[0] = createInfo.colorBlendState.blendConstants[0];
        colorBlendState.blendConstants[1] = createInfo.colorBlendState.blendConstants[1];
        colorBlendState.blendConstants[2] = createInfo.colorBlendState.blendConstants[2];
        colorBlendState.blendConstants[3] = createInfo.colorBlendState.blendConstants[3];
        pipelineInfo.pColorBlendState = &colorBlendState;

        VkPipelineDynamicStateCreateInfo dynamicState = {};
        if (createInfo.dynamicState.dynamicStates.size())
        {
            dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamicState.dynamicStateCount = static_cast<uint32_t>(createInfo.dynamicState.dynamicStates.size());
            dynamicState.pDynamicStates = createInfo.dynamicState.dynamicStates.data();
            pipelineInfo.pDynamicState = &dynamicState;
        }
        else
        {
            pipelineInfo.pDynamicState = nullptr;
        }

        RenderPass* dummyRenderPass = mDevice.getDummyRenderPassCache().getOrCreateDummyRenderPass(createInfo.colorFormats, createInfo.depthStencilFormat, multisampleState.rasterizationSamples);

        pipelineInfo.layout = createInfo.layout->getHandle();
        pipelineInfo.renderPass = dummyRenderPass->getHandle();
        pipelineInfo.subpass = createInfo.subpass;
        if (createInfo.basePipeline)
        {
            pipelineInfo.basePipelineHandle = createInfo.basePipeline->getHandle();
            pipelineInfo.basePipelineIndex = -1;
        }

        /*VkPipelineRenderingCreateInfo pipelineRenderingInfo = {};
        pipelineRenderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        pipelineRenderingInfo.colorAttachmentCount = 1;
        pipelineRenderingInfo.pColorAttachmentFormats = ;
        pipelineRenderingInfo.depthAttachmentFormat = ;
        pipelineRenderingInfo.stencilAttachmentFormat = ;*/

        vkCreateGraphicsPipelines(mDevice.getHandle(), pipelineCache, 1, &pipelineInfo, nullptr, &mPipeline);
    }

    Pipeline::Pipeline(const Device& device, const ComputeCreateInfo& createInfo, VkPipelineCache pipelineCache)
        : mDevice(device), mLayout(createInfo.layout), mBindPoint(VK_PIPELINE_BIND_POINT_COMPUTE)
    {
        VkComputePipelineCreateInfo pipelineInfo = {};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;

        pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineInfo.stage.stage = createInfo.shaderStage.stage;
        pipelineInfo.stage.module = createInfo.shaderStage.module;
        pipelineInfo.stage.pName = createInfo.shaderStage.name;

        pipelineInfo.layout = createInfo.layout->getHandle();

        if (createInfo.basePipeline)
        {
            pipelineInfo.basePipelineHandle = createInfo.basePipeline->getHandle();
            pipelineInfo.basePipelineIndex = -1;
        }

        vkCreateComputePipelines(mDevice.getHandle(), pipelineCache, 1, &pipelineInfo, nullptr, &mPipeline);
    }

    Pipeline::Pipeline(const Device& device, const RayTracingCreateInfo& createInfo, VkPipelineCache pipelineCache)
        : mDevice(device), mLayout(createInfo.layout), mBindPoint(VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR)
    {
        VkRayTracingPipelineCreateInfoKHR pipelineInfo = {};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;

        std::vector<VkPipelineShaderStageCreateInfo> shaderStages(createInfo.shaderStages.size());
        for (uint32_t i = 0; i < shaderStages.size(); ++i)
        {
            shaderStages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shaderStages[i].stage = createInfo.shaderStages[i].stage;
            shaderStages[i].module = createInfo.shaderStages[i].module;
            shaderStages[i].pName = createInfo.shaderStages[i].name;
        }
        pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineInfo.pStages = shaderStages.data();

        std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups(createInfo.shaderGroups.size());
        for (uint32_t i = 0; i < shaderGroups.size(); ++i)
        {
            shaderGroups[i].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
            shaderGroups[i].type = createInfo.shaderGroups[i].type;
            shaderGroups[i].generalShader = createInfo.shaderGroups[i].generalShader;
            shaderGroups[i].closestHitShader = createInfo.shaderGroups[i].closestHitShader;
            shaderGroups[i].anyHitShader = createInfo.shaderGroups[i].anyHitShader;
            shaderGroups[i].intersectionShader = createInfo.shaderGroups[i].intersectionShader;
        }
        pipelineInfo.groupCount = static_cast<uint32_t>(shaderGroups.size());
        pipelineInfo.pGroups = shaderGroups.data();

        pipelineInfo.maxPipelineRayRecursionDepth = createInfo.maxPipelineRayRecursionDepth;
        pipelineInfo.layout = createInfo.layout->getHandle();

        vkCreateRayTracingPipelinesKHR(
            mDevice.getHandle(),
            VK_NULL_HANDLE,
            pipelineCache,
            1,
            &pipelineInfo,
            nullptr,
            &mPipeline
        );
    }

    Pipeline::Pipeline(
        const Device& device,
        PipelineLayout* layout,
        VkPipeline pipeline,
        VkPipelineBindPoint bindPoint
    ) : mDevice(device), mLayout(layout), mPipeline(pipeline), mBindPoint(bindPoint)
    {
    }

    Pipeline::~Pipeline()
    {
        vkDestroyPipeline(mDevice.getHandle(), mPipeline, nullptr);
    }

    std::vector<Pipeline*> Pipeline::createPipelines(const Device& device, std::span<const GraphicsCreateInfo> createInfos, VkPipelineCache pipelineCache)
    {
        std::vector<Pipeline*> result;
        uint32_t pipelineCount = static_cast<uint32_t>(createInfos.size());
        std::vector<VkGraphicsPipelineCreateInfo> pipelineInfos(pipelineCount);
        std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
        {
            uint32_t shaderStageCount = 0;
            for (auto& createInfo : createInfos)
                shaderStageCount += static_cast<uint32_t>(createInfo.shaderStages.size());
            shaderStages.resize(shaderStageCount);
        }
        std::vector<VkPipelineVertexInputStateCreateInfo> vertexInputStates(pipelineCount);
        std::vector<VkPipelineInputAssemblyStateCreateInfo> inputAssemblyStates(pipelineCount);
        std::vector<VkPipelineTessellationStateCreateInfo> tessellationStates(pipelineCount);
        std::vector<VkPipelineViewportStateCreateInfo> viewportStates(pipelineCount);
        std::vector<VkPipelineRasterizationStateCreateInfo> rasterizationStates(pipelineCount);
        std::vector<VkPipelineMultisampleStateCreateInfo> multisampleStates(pipelineCount);
        std::vector<VkPipelineDepthStencilStateCreateInfo> depthStencilStates(pipelineCount);
        std::vector<VkPipelineColorBlendStateCreateInfo> colorBlendStates(pipelineCount);
        std::vector<VkPipelineDynamicStateCreateInfo> dynamicStates(pipelineCount);

        uint32_t shaderStageOffset = 0;
        for (uint32_t i = 0; i < pipelineCount; ++i)
        {
            auto& createInfo = createInfos[i];
            auto& pipelineInfo = pipelineInfos[i];

            VkShaderStageFlags allStageFlags = 0;
            uint32_t shaderStageCount = static_cast<uint32_t>(createInfo.shaderStages.size());
            for (uint32_t i = 0; i < shaderStageCount; ++i)
            {
                auto& shaderStage = shaderStages[shaderStageOffset + i];
                shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                shaderStage.stage = createInfo.shaderStages[i].stage;
                shaderStage.module = createInfo.shaderStages[i].module;
                shaderStage.pName = createInfo.shaderStages[i].name;
                allStageFlags |= shaderStages[i].stage;
            }
            pipelineInfo.stageCount = shaderStageCount;
            pipelineInfo.pStages = shaderStages.data() + shaderStageOffset;
            shaderStageOffset += shaderStageCount;

            auto& vertexInputState = vertexInputStates[i];
            vertexInputState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertexInputState.vertexBindingDescriptionCount = static_cast<uint32_t>(createInfo.vertexInputState.bindings.size());
            vertexInputState.pVertexBindingDescriptions = createInfo.vertexInputState.bindings.data();
            vertexInputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(createInfo.vertexInputState.attributes.size());
            vertexInputState.pVertexAttributeDescriptions = createInfo.vertexInputState.attributes.data();
            pipelineInfo.pVertexInputState = &vertexInputState;

            auto& inputAssemblyState = inputAssemblyStates[i];
            inputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            inputAssemblyState.topology = createInfo.inputAssemblyState.topology;
            inputAssemblyState.primitiveRestartEnable = createInfo.inputAssemblyState.primitiveRestartEnable;
            pipelineInfo.pInputAssemblyState = &inputAssemblyState;

            auto& tessellationState = tessellationStates[i];
            if (allStageFlags & (VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT))
            {
                tessellationState.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
                tessellationState.patchControlPoints = createInfo.tessellationState.patchControlPoints;
                pipelineInfo.pTessellationState = &tessellationState;
            }
            else
            {
                pipelineInfo.pTessellationState = nullptr;
            }

            auto& viewportState = viewportStates[i];
            viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewportState.viewportCount = static_cast<uint32_t>(createInfo.viewportState.viewports.size());
            viewportState.pViewports = createInfo.viewportState.viewports.data();
            viewportState.scissorCount = static_cast<uint32_t>(createInfo.viewportState.scissors.size());
            viewportState.pScissors = createInfo.viewportState.scissors.data();
            pipelineInfo.pViewportState = &viewportState;

            auto& rasterizationState = rasterizationStates[i];
            rasterizationState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterizationState.depthClampEnable = createInfo.rasterizationState.depthClampEnable;
            rasterizationState.rasterizerDiscardEnable = createInfo.rasterizationState.rasterizerDiscardEnable;
            rasterizationState.polygonMode = createInfo.rasterizationState.polygonMode;
            rasterizationState.cullMode = createInfo.rasterizationState.cullMode;
            rasterizationState.frontFace = createInfo.rasterizationState.frontFace;
            rasterizationState.depthBiasEnable = createInfo.rasterizationState.depthBiasEnable;
            rasterizationState.depthBiasConstantFactor = createInfo.rasterizationState.depthBiasConstantFactor;
            rasterizationState.depthBiasClamp = createInfo.rasterizationState.depthBiasClamp;
            rasterizationState.depthBiasSlopeFactor = createInfo.rasterizationState.depthBiasSlopeFactor;
            rasterizationState.lineWidth = createInfo.rasterizationState.lineWidth;
            pipelineInfo.pRasterizationState = &rasterizationState;

            auto& multisampleState = multisampleStates[i];
            multisampleState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisampleState.rasterizationSamples = createInfo.multisampleState.rasterizationSamples;
            multisampleState.sampleShadingEnable = createInfo.multisampleState.sampleShadingEnable;
            multisampleState.minSampleShading = createInfo.multisampleState.minSampleShading;
            multisampleState.alphaToCoverageEnable = createInfo.multisampleState.alphaToCoverageEnable;
            multisampleState.alphaToOneEnable = createInfo.multisampleState.alphaToOneEnable;
            pipelineInfo.pMultisampleState = &multisampleState;

            auto& depthStencilState = depthStencilStates[i];
            depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depthStencilState.depthTestEnable = createInfo.depthStencilState.depthTestEnable;
            depthStencilState.depthWriteEnable = createInfo.depthStencilState.depthWriteEnable;
            depthStencilState.depthCompareOp = createInfo.depthStencilState.depthCompareOp;
            depthStencilState.depthBoundsTestEnable = createInfo.depthStencilState.depthBoundsTestEnable;
            depthStencilState.stencilTestEnable = createInfo.depthStencilState.stencilTestEnable;
            depthStencilState.front = createInfo.depthStencilState.front;
            depthStencilState.back = createInfo.depthStencilState.back;
            depthStencilState.minDepthBounds = createInfo.depthStencilState.minDepthBounds;
            depthStencilState.maxDepthBounds = createInfo.depthStencilState.maxDepthBounds;
            pipelineInfo.pDepthStencilState = &depthStencilState;

            auto& colorBlendState = colorBlendStates[i];
            colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            colorBlendState.logicOpEnable = createInfo.colorBlendState.logicOpEnable;
            colorBlendState.logicOp = createInfo.colorBlendState.logicOp;
            colorBlendState.attachmentCount = static_cast<uint32_t>(createInfo.colorBlendState.attachments.size());
            colorBlendState.pAttachments = createInfo.colorBlendState.attachments.data();
            colorBlendState.blendConstants[0] = createInfo.colorBlendState.blendConstants[0];
            colorBlendState.blendConstants[1] = createInfo.colorBlendState.blendConstants[1];
            colorBlendState.blendConstants[2] = createInfo.colorBlendState.blendConstants[2];
            colorBlendState.blendConstants[3] = createInfo.colorBlendState.blendConstants[3];
            pipelineInfo.pColorBlendState = &colorBlendState;

            auto& dynamicState = dynamicStates[i];
            if (createInfo.dynamicState.dynamicStates.size())
            {
                dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
                dynamicState.dynamicStateCount = static_cast<uint32_t>(createInfo.dynamicState.dynamicStates.size());
                dynamicState.pDynamicStates = createInfo.dynamicState.dynamicStates.data();
                pipelineInfo.pDynamicState = &dynamicState;
            }
            else
            {
                pipelineInfo.pDynamicState = nullptr;
            }

            RenderPass* dummyRenderPass = device.getDummyRenderPassCache().getOrCreateDummyRenderPass(createInfo.colorFormats, createInfo.depthStencilFormat, multisampleState.rasterizationSamples);

            pipelineInfo.layout = createInfo.layout->getHandle();
            pipelineInfo.renderPass = dummyRenderPass->getHandle();
            pipelineInfo.subpass = createInfo.subpass;
            if (createInfo.basePipeline)
            {
                pipelineInfo.basePipelineHandle = createInfo.basePipeline->getHandle();
                pipelineInfo.basePipelineIndex = -1;
            }
        }

        std::vector<VkPipeline> pipelines(pipelineCount);
        vkCreateGraphicsPipelines(device.getHandle(), pipelineCache, pipelineCount, pipelineInfos.data(), nullptr, pipelines.data());

        result.resize(pipelineCount);
        for (uint32_t i = 0; i < pipelineCount; ++i)
        {
            result[i] = new Pipeline(device, createInfos[i].layout, pipelines[i], VK_PIPELINE_BIND_POINT_GRAPHICS);
        }

        return result;
    }

    std::vector<Pipeline*> Pipeline::createPipelines(const Device& device, std::span<const ComputeCreateInfo> createInfos, VkPipelineCache pipelineCache)
    {
        std::vector<Pipeline*> result;
        uint32_t pipelineCount = static_cast<uint32_t>(createInfos.size());
        std::vector<VkComputePipelineCreateInfo> pipelineInfos(pipelineCount);

        for (uint32_t i = 0; i < pipelineCount; ++i)
        {
            pipelineInfos[i].sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            VkPipelineShaderStageCreateInfo& shaderStage = pipelineInfos[i].stage;
            shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shaderStage.stage = createInfos[i].shaderStage.stage;
            shaderStage.module = createInfos[i].shaderStage.module;
            shaderStage.pName = createInfos[i].shaderStage.name;
            pipelineInfos[i].layout = createInfos[i].layout->getHandle();
            pipelineInfos[i].basePipelineHandle = VK_NULL_HANDLE;
            pipelineInfos[i].basePipelineIndex = -1;
        }

        std::vector<VkPipeline> pipelines(pipelineCount);
        vkCreateComputePipelines(device.getHandle(), pipelineCache, pipelineCount, pipelineInfos.data(), nullptr, pipelines.data());

        result.resize(pipelineCount);
        for (uint32_t i = 0; i < pipelineCount; ++i)
        {
            result[i] = new Pipeline(device, createInfos[i].layout, pipelines[i], VK_PIPELINE_BIND_POINT_COMPUTE);
        }

        return result;
    }

}
