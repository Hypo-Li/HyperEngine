#include "ImGuiBackend.h"

#include <Vulkan/Device.h>
#include <Vulkan/RenderPassCache.h>
#include <Vulkan/Texture.h>
#include <Vulkan/VkFormatUtil.h>
#include <WebGPU/Device.h>
#include <WebGPU/Texture.h>
#include <WebGPU/WgpuFormatUtil.h>

#include <RenderBase/RenderBaseDefines.h>

#include <backend/imgui_impl_vulkan.h>
#include <backend/imgui_impl_wgpu.h>

#include <span>
#include <unordered_map>
#include "ImGuiShadedImageShaders.h"

namespace hyp
{
    namespace
    {
        rhi::Backend gBackend = rhi::Backend::Vulkan;
        bool gAvailable = false;
        // ImGui_ImplVulkan_InitInfo::PipelineInfoMain.PipelineRenderingCreateInfo.
        // pColorAttachmentFormats 要求指向的内存在 backend 生命周期内保持有效, 不能用
        // initRenderer() 调用里的局部变量。
        VkFormat gVulkanColorFormatStorage = VK_FORMAT_UNDEFINED;
        // 供 unregisterTexture() 把旧描述符的销毁丢进 DeferredTaskQueue 延迟执行, 也供
        // shutdownShadedImagePipelines() 重新拿一次 VkDevice。
        RenderBackend* gRenderBackend = nullptr;

        // ---------------------------------------------------------------
        // ShadedImageMode 那族自定义 pipeline(NormalImage() 等, 见 ImGuiShadedImage.h)。
        //
        // 这部分不经过引擎自己的 RenderGraph/PipelineCache/BindGroupLayout 抽象, 全程直接用
        // 原生 Vulkan API, 和 imgui_impl_vulkan.cpp 内部搭 pipeline 的写法保持同一个层级——
        // 理由是: 紧跟在这些自定义绘制之后, imgui 自己还会正常发出这块图像的实际绘制命令
        // (顶点/索引缓冲、texture/sampler descriptor set 都是 imgui 自己绑的), 我们的
        // pipeline layout 必须和 imgui 内部那份严格结构对齐才能兼容, 引擎自己的
        // BindGroupLayout 系统没有办法表达"跟 imgui_impl_vulkan.cpp 内部私有状态完全一致"
        // 这种约束。这也是为什么这套 pipeline 的生命周期挂在 ImGuiBackend 而不是独立模块上:
        // 它用的是同一个 VkDevice、同一份 color format、同一套 imgui 内部 descriptor set
        // 布局约束, 概念上和 registerTexture() 是同一类"后端要处理的、和图形 API 强绑定的
        // 细节", 不是一个可以独立生存的子系统。
        //
        // 具体机制: ImDrawList::AddCallback(&ImGuiBackend::bindShadedImagePipeline, ...) 会在
        // ImGui_ImplVulkan_RenderDrawData() 真正回放这个 drawlist 时被调用, 此时可以从
        // ImGui::GetPlatformIO().Renderer_RenderState(类型 ImGui_ImplVulkan_RenderState)拿到
        // 当前 command buffer, 换上我们自己的 pipeline; 紧跟着的 ImGui::Image() 那条命令仍然
        // 由 imgui 自己发出实际绘制, 只是这时候用的已经是换上去的 pipeline。画完用
        // GetPlatformIO().DrawCallback_ResetRenderState 换回去, 不然会污染后面所有别的 UI
        // 绘制。这是 Dear ImGui 自己文档说明支持的机制(imgui.h 里 AddCallback() 的注释),
        // 不是取巧的做法。
        //
        // pipeline layout 的两段 push constant range: [0,16) VERTEX 阶段, 跟 imgui 自己的
        // 2D 变换 range 完全对齐(offset/size/stage 全部相同)——Vulkan 的 push constant
        // 兼容性规则是"新绑定的 pipeline layout 对某段字节声明了相同的 range, 之前已经推送
        // 的数据依然有效", 所以不需要我们自己重新算一遍 uScale/uTranslate 再推一次, 直接
        // 白嫖 imgui 在这个 drawlist 开始前已经推送好的那份。[16,20) FRAGMENT 阶段是我们
        // 自己的参数(目前只有 channelMask), 和上面那段不重叠, 互不影响。
        // ---------------------------------------------------------------

        bool gShadedImageAvailable = false;
        VkDescriptorSetLayout gDescriptorSetLayoutTexture = VK_NULL_HANDLE;
        VkDescriptorSetLayout gDescriptorSetLayoutSampler = VK_NULL_HANDLE;
        VkPipelineLayout gShadedPipelineLayout = VK_NULL_HANDLE;
        VkShaderModule gShadedVertModule = VK_NULL_HANDLE;
        VkShaderModule gNormalImageFragModule = VK_NULL_HANDLE;
        VkPipeline gNormalImagePipeline = VK_NULL_HANDLE;
        VkShaderModule gGrayImageFragModule = VK_NULL_HANDLE;
        VkPipeline gGrayImagePipeline = VK_NULL_HANDLE;
        VkShaderModule gPlainImageFragModule = VK_NULL_HANDLE;
        VkPipeline gPlainImagePipeline = VK_NULL_HANDLE;
        VkShaderModule gSRGBImageFragModule = VK_NULL_HANDLE;
        VkPipeline gSRGBImagePipeline = VK_NULL_HANDLE;
        VkShaderModule gCubeImageFragModule = VK_NULL_HANDLE;
        VkPipeline gCubeImagePipeline = VK_NULL_HANDLE;

        WGPUBindGroupLayout gWgpuCommonBindGroupLayout = nullptr;
        WGPUBindGroupLayout gWgpuImageBindGroupLayout = nullptr;
        WGPUPipelineLayout gWgpuShadedPipelineLayout = nullptr;
        WGPUShaderModule gWgpuShadedVertModule = nullptr;
        WGPUShaderModule gWgpuNormalImageFragModule = nullptr;
        WGPURenderPipeline gWgpuNormalImagePipeline = nullptr;
        WGPUShaderModule gWgpuGrayImageFragModule = nullptr;
        WGPURenderPipeline gWgpuGrayImagePipeline = nullptr;
        WGPUShaderModule gWgpuPlainImageFragModule = nullptr;
        WGPURenderPipeline gWgpuPlainImagePipeline = nullptr;
        WGPUShaderModule gWgpuSRGBImageFragModule = nullptr;
        WGPURenderPipeline gWgpuSRGBImagePipeline = nullptr;

        // Cube 模式在 WGPU 下的 group 1 跟另外 4 个模式不兼容(viewDimension 是 Cube 不是
        // 2D), 需要自己单独一份 bind group layout/pipeline layout, 见
        // createCubeImagePipelineWGPU()。
        WGPUBindGroupLayout gWgpuCubeImageBindGroupLayout = nullptr;
        WGPUPipelineLayout gWgpuCubePipelineLayout = nullptr;
        WGPUShaderModule gWgpuCubeImageFragModule = nullptr;
        WGPURenderPipeline gWgpuCubeImagePipeline = nullptr;
        // Cube 模式故意绕开了 imgui_impl_wgpu.cpp 自己那份按 tex_id 缓存 bind group 的机制
        // (见 bindShadedImagePipeline() 的用法), 所以要自己缓存一份, 按 WGPUTextureView
        // 的句柄值查找/创建, 生命周期跟着 ImGuiBackend 走, 在 shutdownShadedImagePipelinesWGPU()
        // 里统一释放。
        std::unordered_map<WGPUTextureView, WGPUBindGroup> gWgpuCubeBindGroupCache;

        void createShadedImageDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout* layout, VkDescriptorType descriptorType)
        {
            VkDescriptorSetLayoutBinding binding{};
            binding.descriptorType = descriptorType;
            binding.descriptorCount = 1;
            binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

            VkDescriptorSetLayoutCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            info.bindingCount = 1;
            info.pBindings = &binding;
            vkCreateDescriptorSetLayout(device, &info, nullptr, layout);
        }

        VkShaderModule createShadedImageShaderModule(VkDevice device, std::span<const uint32_t> spirv)
        {
            VkShaderModuleCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            info.codeSize = spirv.size_bytes();
            info.pCode = spirv.data();
            VkShaderModule module = VK_NULL_HANDLE;
            vkCreateShaderModule(device, &info, nullptr, &module);
            return module;
        }

        VkPipeline createShadedImagePipeline(VkDevice device, VkShaderModule fragModule, const vk::Device& vkDevice, rhi::TextureFormat colorFormat)
        {
            VkPipelineShaderStageCreateInfo stage[2]{};
            stage[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stage[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            stage[0].module = gShadedVertModule;
            stage[0].pName = "main";
            stage[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stage[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            stage[1].module = fragModule;
            stage[1].pName = "main";

            // 顶点布局/混合/光栅化状态照抄 imgui_impl_vulkan.cpp::ImGui_ImplVulkan_CreatePipeline()——
            // 后面那条正常的 ImGui::Image() 绘制命令喂给我们这个 pipeline 的顶点数据就是 ImDrawVert,
            // 必须严格匹配它的内存布局(pos/uv 各一个 vec2, col 是压成一个 u32 的 RGBA8_UNORM)。
            VkVertexInputBindingDescription bindingDesc{};
            bindingDesc.stride = sizeof(ImDrawVert);
            bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            VkVertexInputAttributeDescription attributeDesc[3]{};
            attributeDesc[0].location = 0;
            attributeDesc[0].format = VK_FORMAT_R32G32_SFLOAT;
            attributeDesc[0].offset = offsetof(ImDrawVert, pos);
            attributeDesc[1].location = 1;
            attributeDesc[1].format = VK_FORMAT_R32G32_SFLOAT;
            attributeDesc[1].offset = offsetof(ImDrawVert, uv);
            attributeDesc[2].location = 2;
            attributeDesc[2].format = VK_FORMAT_R8G8B8A8_UNORM;
            attributeDesc[2].offset = offsetof(ImDrawVert, col);

            VkPipelineVertexInputStateCreateInfo vertexInfo{};
            vertexInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertexInfo.vertexBindingDescriptionCount = 1;
            vertexInfo.pVertexBindingDescriptions = &bindingDesc;
            vertexInfo.vertexAttributeDescriptionCount = 3;
            vertexInfo.pVertexAttributeDescriptions = attributeDesc;

            VkPipelineInputAssemblyStateCreateInfo iaInfo{};
            iaInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            iaInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

            VkPipelineViewportStateCreateInfo viewportInfo{};
            viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewportInfo.viewportCount = 1;
            viewportInfo.scissorCount = 1;

            VkPipelineRasterizationStateCreateInfo rasterInfo{};
            rasterInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterInfo.polygonMode = VK_POLYGON_MODE_FILL;
            rasterInfo.cullMode = VK_CULL_MODE_NONE;
            rasterInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            rasterInfo.lineWidth = 1.0f;

            VkPipelineMultisampleStateCreateInfo msInfo{};
            msInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            msInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            VkPipelineColorBlendAttachmentState colorAttachment{};
            colorAttachment.blendEnable = VK_TRUE;
            colorAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            colorAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

            VkPipelineDepthStencilStateCreateInfo depthInfo{};
            depthInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

            VkPipelineColorBlendStateCreateInfo blendInfo{};
            blendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            blendInfo.attachmentCount = 1;
            blendInfo.pAttachments = &colorAttachment;

            VkDynamicState dynamicStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
            VkPipelineDynamicStateCreateInfo dynamicState{};
            dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamicState.dynamicStateCount = 2;
            dynamicState.pDynamicStates = dynamicStates;

            VkGraphicsPipelineCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            createInfo.stageCount = 2;
            createInfo.pStages = stage;
            createInfo.pVertexInputState = &vertexInfo;
            createInfo.pInputAssemblyState = &iaInfo;
            createInfo.pViewportState = &viewportInfo;
            createInfo.pRasterizationState = &rasterInfo;
            createInfo.pMultisampleState = &msInfo;
            createInfo.pDepthStencilState = &depthInfo;
            createInfo.pColorBlendState = &blendInfo;
            createInfo.pDynamicState = &dynamicState;
            createInfo.layout = gShadedPipelineLayout;

            VkFormat vkColorFormat = vk::convertToVkFormat(colorFormat);
            VkPipelineRenderingCreateInfoKHR renderingInfo{};
            bool useDynamicRendering = vkDevice.getVkFeatures().dynamicRendering;
            if (useDynamicRendering)
            {
                renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
                renderingInfo.colorAttachmentCount = 1;
                renderingInfo.pColorAttachmentFormats = &vkColorFormat;
                createInfo.pNext = &renderingInfo;
            }
            else
            {
                // 和下面 Vulkan 分支里 imgui 自己那份走同一个 RenderPassCache, 按 format
                // 缓存查找, 拿到的是同一个 VkRenderPass, 不会重复创建
                createInfo.renderPass = vkDevice.getRenderPassCache().getOrCreateRenderPass(
                    { &vkColorFormat, 1 }, VK_FORMAT_UNDEFINED, VK_SAMPLE_COUNT_1_BIT);
            }

            VkPipeline pipeline = VK_NULL_HANDLE;
            vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &createInfo, nullptr, &pipeline);
            return pipeline;
        }

        void initShadedImagePipelines(const vk::Device& vkDevice, rhi::TextureFormat colorFormat)
        {
            VkDevice device = vkDevice.getHandle();

            createShadedImageDescriptorSetLayout(device, &gDescriptorSetLayoutTexture, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
            createShadedImageDescriptorSetLayout(device, &gDescriptorSetLayoutSampler, VK_DESCRIPTOR_TYPE_SAMPLER);

            // 只能声明这一段, 跟 imgui 自己 PipelineLayout 的 push constant range 必须完全
            // 一致(数量/offset/size/stageFlags 逐项相同)——Vulkan 的 pipeline layout
            // "兼容性"判定把 push constant range 和 descriptor set 定义绑在一起判断, 哪怕
            // descriptor set 定义完全一样, push constant range 只要有任何差异(哪怕是纯粹
            // 新增、不重叠的一段), 后续 imgui 自己发出的 vkCmdBindDescriptorSets 也会被判定
            // 跟当前绑定的 pipeline layout 不兼容, 连累已经绑定的 set 0/1 都失效
            // (实测触发过 VUID-vkCmdDrawIndexed-None-08600)。channelMask 这类我们自己的
            // 参数不能走这条路, 要走 descriptor set(见 TODO: 加一个 imgui 不知道的 set 2)。
            VkPushConstantRange pushConstants[1]{};
            pushConstants[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
            pushConstants[0].offset = 0;
            pushConstants[0].size = sizeof(float) * 4; // uScale + uTranslate, 跟 imgui 自己那段完全对齐

            VkDescriptorSetLayout setLayouts[2] = { gDescriptorSetLayoutTexture, gDescriptorSetLayoutSampler };
            VkPipelineLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layoutInfo.setLayoutCount = 2;
            layoutInfo.pSetLayouts = setLayouts;
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = pushConstants;
            vkCreatePipelineLayout(device, &layoutInfo, nullptr, &gShadedPipelineLayout);

            gShadedVertModule = createShadedImageShaderModule(device, kShadedImageVertSPV);
            gNormalImageFragModule = createShadedImageShaderModule(device, kNormalImageFragSPV);
            gNormalImagePipeline = createShadedImagePipeline(device, gNormalImageFragModule, vkDevice, colorFormat);
            gGrayImageFragModule = createShadedImageShaderModule(device, kGrayImageFragSPV);
            gGrayImagePipeline = createShadedImagePipeline(device, gGrayImageFragModule, vkDevice, colorFormat);
            gPlainImageFragModule = createShadedImageShaderModule(device, kPlainImageFragSPV);
            gPlainImagePipeline = createShadedImagePipeline(device, gPlainImageFragModule, vkDevice, colorFormat);
            gSRGBImageFragModule = createShadedImageShaderModule(device, kSRGBImageFragSPV);
            gSRGBImagePipeline = createShadedImagePipeline(device, gSRGBImageFragModule, vkDevice, colorFormat);
            gCubeImageFragModule = createShadedImageShaderModule(device, kCubeImageFragSPV);
            gCubeImagePipeline = createShadedImagePipeline(device, gCubeImageFragModule, vkDevice, colorFormat);

            gShadedImageAvailable = true;
        }

        void shutdownShadedImagePipelines()
        {
            if (!gShadedImageAvailable)
                return;
            gShadedImageAvailable = false;

            const auto& vkDevice = static_cast<const vk::Device&>(gRenderBackend->getDevice());
            VkDevice device = vkDevice.getHandle();

            vkDestroyPipeline(device, gNormalImagePipeline, nullptr);
            vkDestroyShaderModule(device, gNormalImageFragModule, nullptr);
            vkDestroyPipeline(device, gGrayImagePipeline, nullptr);
            vkDestroyShaderModule(device, gGrayImageFragModule, nullptr);
            vkDestroyPipeline(device, gPlainImagePipeline, nullptr);
            vkDestroyShaderModule(device, gPlainImageFragModule, nullptr);
            vkDestroyPipeline(device, gSRGBImagePipeline, nullptr);
            vkDestroyShaderModule(device, gSRGBImageFragModule, nullptr);
            vkDestroyPipeline(device, gCubeImagePipeline, nullptr);
            vkDestroyShaderModule(device, gCubeImageFragModule, nullptr);
            vkDestroyShaderModule(device, gShadedVertModule, nullptr);
            vkDestroyPipelineLayout(device, gShadedPipelineLayout, nullptr);
            vkDestroyDescriptorSetLayout(device, gDescriptorSetLayoutSampler, nullptr);
            vkDestroyDescriptorSetLayout(device, gDescriptorSetLayoutTexture, nullptr);

            gNormalImagePipeline = VK_NULL_HANDLE;
            gNormalImageFragModule = VK_NULL_HANDLE;
            gGrayImagePipeline = VK_NULL_HANDLE;
            gGrayImageFragModule = VK_NULL_HANDLE;
            gPlainImagePipeline = VK_NULL_HANDLE;
            gPlainImageFragModule = VK_NULL_HANDLE;
            gSRGBImagePipeline = VK_NULL_HANDLE;
            gSRGBImageFragModule = VK_NULL_HANDLE;
            gCubeImagePipeline = VK_NULL_HANDLE;
            gCubeImageFragModule = VK_NULL_HANDLE;
            gShadedVertModule = VK_NULL_HANDLE;
            gShadedPipelineLayout = VK_NULL_HANDLE;
            gDescriptorSetLayoutSampler = VK_NULL_HANDLE;
            gDescriptorSetLayoutTexture = VK_NULL_HANDLE;
        }

        WGPUShaderModule createShadedImageShaderModuleWGSL(WGPUDevice device, const char* wgslSource)
        {
            WGPUShaderSourceWGSL wgslDesc{};
            wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
            wgslDesc.code = { wgslSource, WGPU_STRLEN };

            WGPUShaderModuleDescriptor desc{};
            desc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslDesc);
            return wgpuDeviceCreateShaderModule(device, &desc);
        }

        // group(0)/group(1) 的绑定结构照抄 imgui_impl_wgpu.cpp::ImGui_ImplWGPU_CreateDeviceObjects()
        // 里的 common_bg_layout/image_bg_layout——imgui 自己在渲染这个 drawlist 时会用它私有的
        // 那份布局把 uniform/sampler(group 0)和纹理(group 1)绑好, 我们的 pipeline layout 必须
        // 逐项结构一致(WebGPU 按内容判定 bind group 是否兼容, 不要求同一个 layout 句柄)才能
        // 复用 imgui 已经绑定好的这两个 group, 不用自己重新绑一次。
        WGPURenderPipeline createShadedImagePipelineWGPU(WGPUDevice device, WGPUShaderModule fragModule, WGPUTextureFormat colorFormat)
        {
            WGPUBindGroupLayoutEntry commonEntries[2]{};
            commonEntries[0].binding = 0;
            commonEntries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
            commonEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
            commonEntries[1].binding = 1;
            commonEntries[1].visibility = WGPUShaderStage_Fragment;
            commonEntries[1].sampler.type = WGPUSamplerBindingType_Filtering;

            WGPUBindGroupLayoutEntry imageEntry{};
            imageEntry.binding = 0;
            imageEntry.visibility = WGPUShaderStage_Fragment;
            imageEntry.texture.sampleType = WGPUTextureSampleType_Float;
            imageEntry.texture.viewDimension = WGPUTextureViewDimension_2D;

            WGPUBindGroupLayoutDescriptor commonLayoutDesc{};
            commonLayoutDesc.entryCount = 2;
            commonLayoutDesc.entries = commonEntries;
            gWgpuCommonBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &commonLayoutDesc);

            WGPUBindGroupLayoutDescriptor imageLayoutDesc{};
            imageLayoutDesc.entryCount = 1;
            imageLayoutDesc.entries = &imageEntry;
            gWgpuImageBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &imageLayoutDesc);

            WGPUBindGroupLayout bgLayouts[2] = { gWgpuCommonBindGroupLayout, gWgpuImageBindGroupLayout };
            WGPUPipelineLayoutDescriptor pipelineLayoutDesc{};
            pipelineLayoutDesc.bindGroupLayoutCount = 2;
            pipelineLayoutDesc.bindGroupLayouts = bgLayouts;
            gWgpuShadedPipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

            WGPUVertexAttribute attributes[3]{};
            attributes[0].format = WGPUVertexFormat_Float32x2;
            attributes[0].offset = offsetof(ImDrawVert, pos);
            attributes[0].shaderLocation = 0;
            attributes[1].format = WGPUVertexFormat_Float32x2;
            attributes[1].offset = offsetof(ImDrawVert, uv);
            attributes[1].shaderLocation = 1;
            attributes[2].format = WGPUVertexFormat_Unorm8x4;
            attributes[2].offset = offsetof(ImDrawVert, col);
            attributes[2].shaderLocation = 2;

            WGPUVertexBufferLayout bufferLayout{};
            bufferLayout.arrayStride = sizeof(ImDrawVert);
            bufferLayout.stepMode = WGPUVertexStepMode_Vertex;
            bufferLayout.attributeCount = 3;
            bufferLayout.attributes = attributes;

            WGPUBlendState blendState{};
            blendState.color.operation = WGPUBlendOperation_Add;
            blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
            blendState.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
            blendState.alpha.operation = WGPUBlendOperation_Add;
            blendState.alpha.srcFactor = WGPUBlendFactor_One;
            blendState.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;

            WGPUColorTargetState colorTarget{};
            colorTarget.format = colorFormat;
            colorTarget.blend = &blendState;
            colorTarget.writeMask = WGPUColorWriteMask_All;

            WGPUFragmentState fragmentState{};
            fragmentState.module = fragModule;
            fragmentState.entryPoint = { "main", WGPU_STRLEN };
            fragmentState.targetCount = 1;
            fragmentState.targets = &colorTarget;

            WGPURenderPipelineDescriptor pipelineDesc{};
            pipelineDesc.layout = gWgpuShadedPipelineLayout;
            pipelineDesc.vertex.module = gWgpuShadedVertModule;
            pipelineDesc.vertex.entryPoint = { "main", WGPU_STRLEN };
            pipelineDesc.vertex.bufferCount = 1;
            pipelineDesc.vertex.buffers = &bufferLayout;
            pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            pipelineDesc.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
            pipelineDesc.primitive.frontFace = WGPUFrontFace_CW;
            pipelineDesc.primitive.cullMode = WGPUCullMode_None;
            pipelineDesc.multisample.count = 1;
            pipelineDesc.multisample.mask = UINT32_MAX;
            pipelineDesc.fragment = &fragmentState;

            return wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);
        }

        // Cube 模式的 group 1 是 textureCube, 跟另外 4 个模式共用的 gWgpuImageBindGroupLayout
        // (viewDimension 固定 2D)不兼容, 单独建一份 pipeline layout——顶点布局/混合/光栅化
        // 状态照抄 createShadedImagePipelineWGPU(), group 0 直接复用已经建好的
        // gWgpuCommonBindGroupLayout(跟维度无关, 不用重建)。
        WGPURenderPipeline createCubeImagePipelineWGPU(WGPUDevice device, WGPUShaderModule fragModule, WGPUTextureFormat colorFormat)
        {
            WGPUBindGroupLayoutEntry cubeImageEntry{};
            cubeImageEntry.binding = 0;
            cubeImageEntry.visibility = WGPUShaderStage_Fragment;
            cubeImageEntry.texture.sampleType = WGPUTextureSampleType_Float;
            cubeImageEntry.texture.viewDimension = WGPUTextureViewDimension_Cube;

            WGPUBindGroupLayoutDescriptor cubeImageLayoutDesc{};
            cubeImageLayoutDesc.entryCount = 1;
            cubeImageLayoutDesc.entries = &cubeImageEntry;
            gWgpuCubeImageBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &cubeImageLayoutDesc);

            WGPUBindGroupLayout bgLayouts[2] = { gWgpuCommonBindGroupLayout, gWgpuCubeImageBindGroupLayout };
            WGPUPipelineLayoutDescriptor pipelineLayoutDesc{};
            pipelineLayoutDesc.bindGroupLayoutCount = 2;
            pipelineLayoutDesc.bindGroupLayouts = bgLayouts;
            gWgpuCubePipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

            WGPUVertexAttribute attributes[3]{};
            attributes[0].format = WGPUVertexFormat_Float32x2;
            attributes[0].offset = offsetof(ImDrawVert, pos);
            attributes[0].shaderLocation = 0;
            attributes[1].format = WGPUVertexFormat_Float32x2;
            attributes[1].offset = offsetof(ImDrawVert, uv);
            attributes[1].shaderLocation = 1;
            attributes[2].format = WGPUVertexFormat_Unorm8x4;
            attributes[2].offset = offsetof(ImDrawVert, col);
            attributes[2].shaderLocation = 2;

            WGPUVertexBufferLayout bufferLayout{};
            bufferLayout.arrayStride = sizeof(ImDrawVert);
            bufferLayout.stepMode = WGPUVertexStepMode_Vertex;
            bufferLayout.attributeCount = 3;
            bufferLayout.attributes = attributes;

            WGPUBlendState blendState{};
            blendState.color.operation = WGPUBlendOperation_Add;
            blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
            blendState.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
            blendState.alpha.operation = WGPUBlendOperation_Add;
            blendState.alpha.srcFactor = WGPUBlendFactor_One;
            blendState.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;

            WGPUColorTargetState colorTarget{};
            colorTarget.format = colorFormat;
            colorTarget.blend = &blendState;
            colorTarget.writeMask = WGPUColorWriteMask_All;

            WGPUFragmentState fragmentState{};
            fragmentState.module = fragModule;
            fragmentState.entryPoint = { "main", WGPU_STRLEN };
            fragmentState.targetCount = 1;
            fragmentState.targets = &colorTarget;

            WGPURenderPipelineDescriptor pipelineDesc{};
            pipelineDesc.layout = gWgpuCubePipelineLayout;
            pipelineDesc.vertex.module = gWgpuShadedVertModule;
            pipelineDesc.vertex.entryPoint = { "main", WGPU_STRLEN };
            pipelineDesc.vertex.bufferCount = 1;
            pipelineDesc.vertex.buffers = &bufferLayout;
            pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            pipelineDesc.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
            pipelineDesc.primitive.frontFace = WGPUFrontFace_CW;
            pipelineDesc.primitive.cullMode = WGPUCullMode_None;
            pipelineDesc.multisample.count = 1;
            pipelineDesc.multisample.mask = UINT32_MAX;
            pipelineDesc.fragment = &fragmentState;

            return wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);
        }

        void initShadedImagePipelinesWGPU(WGPUDevice device, WGPUTextureFormat colorFormat)
        {
            gWgpuShadedVertModule = createShadedImageShaderModuleWGSL(device, kShadedImageVertWGSL);

            gWgpuNormalImageFragModule = createShadedImageShaderModuleWGSL(device, kNormalImageFragWGSL);
            gWgpuNormalImagePipeline = createShadedImagePipelineWGPU(device, gWgpuNormalImageFragModule, colorFormat);
            gWgpuGrayImageFragModule = createShadedImageShaderModuleWGSL(device, kGrayImageFragWGSL);
            gWgpuGrayImagePipeline = createShadedImagePipelineWGPU(device, gWgpuGrayImageFragModule, colorFormat);
            gWgpuPlainImageFragModule = createShadedImageShaderModuleWGSL(device, kPlainImageFragWGSL);
            gWgpuPlainImagePipeline = createShadedImagePipelineWGPU(device, gWgpuPlainImageFragModule, colorFormat);
            gWgpuSRGBImageFragModule = createShadedImageShaderModuleWGSL(device, kSRGBImageFragWGSL);
            gWgpuSRGBImagePipeline = createShadedImagePipelineWGPU(device, gWgpuSRGBImageFragModule, colorFormat);
            gWgpuCubeImageFragModule = createShadedImageShaderModuleWGSL(device, kCubeImageFragWGSL);
            gWgpuCubeImagePipeline = createCubeImagePipelineWGPU(device, gWgpuCubeImageFragModule, colorFormat);

            gShadedImageAvailable = true;
        }

        void shutdownShadedImagePipelinesWGPU()
        {
            if (!gShadedImageAvailable)
                return;
            gShadedImageAvailable = false;

            for (auto& [view, bindGroup] : gWgpuCubeBindGroupCache)
                wgpuBindGroupRelease(bindGroup);
            gWgpuCubeBindGroupCache.clear();

            wgpuRenderPipelineRelease(gWgpuCubeImagePipeline);
            wgpuShaderModuleRelease(gWgpuCubeImageFragModule);
            wgpuPipelineLayoutRelease(gWgpuCubePipelineLayout);
            wgpuBindGroupLayoutRelease(gWgpuCubeImageBindGroupLayout);
            gWgpuCubeImagePipeline = nullptr;
            gWgpuCubeImageFragModule = nullptr;
            gWgpuCubePipelineLayout = nullptr;
            gWgpuCubeImageBindGroupLayout = nullptr;

            wgpuRenderPipelineRelease(gWgpuNormalImagePipeline);
            wgpuShaderModuleRelease(gWgpuNormalImageFragModule);
            wgpuRenderPipelineRelease(gWgpuGrayImagePipeline);
            wgpuShaderModuleRelease(gWgpuGrayImageFragModule);
            wgpuRenderPipelineRelease(gWgpuPlainImagePipeline);
            wgpuShaderModuleRelease(gWgpuPlainImageFragModule);
            wgpuRenderPipelineRelease(gWgpuSRGBImagePipeline);
            wgpuShaderModuleRelease(gWgpuSRGBImageFragModule);
            wgpuShaderModuleRelease(gWgpuShadedVertModule);
            wgpuPipelineLayoutRelease(gWgpuShadedPipelineLayout);
            wgpuBindGroupLayoutRelease(gWgpuImageBindGroupLayout);
            wgpuBindGroupLayoutRelease(gWgpuCommonBindGroupLayout);

            gWgpuNormalImagePipeline = nullptr;
            gWgpuNormalImageFragModule = nullptr;
            gWgpuGrayImagePipeline = nullptr;
            gWgpuGrayImageFragModule = nullptr;
            gWgpuPlainImagePipeline = nullptr;
            gWgpuPlainImageFragModule = nullptr;
            gWgpuSRGBImagePipeline = nullptr;
            gWgpuSRGBImageFragModule = nullptr;
            gWgpuShadedVertModule = nullptr;
            gWgpuShadedPipelineLayout = nullptr;
            gWgpuImageBindGroupLayout = nullptr;
            gWgpuCommonBindGroupLayout = nullptr;
        }
    }

    void ImGuiBackend::initRenderer(RenderBackend& renderBackend, rhi::TextureFormat colorFormat)
    {
        gRenderBackend = &renderBackend;
        gBackend = renderBackend.getRHIBackend();

        if (gBackend == rhi::Backend::Vulkan)
        {
            const auto& vkDevice = static_cast<const vk::Device&>(renderBackend.getDevice());
            const bool useDynamicRendering = vkDevice.getVkFeatures().dynamicRendering;

            vk::Device::QueueInfo queueInfo = vkDevice.getGraphicsQueueInfo();
            gVulkanColorFormatStorage = vk::convertToVkFormat(colorFormat);

            ImGui_ImplVulkan_InitInfo initInfo{};
            initInfo.ApiVersion = vkDevice.getApiVersion();
            initInfo.Instance = vkDevice.getInstance();
            initInfo.PhysicalDevice = vkDevice.getPhysicalDevice();
            initInfo.Device = vkDevice.getHandle();
            initInfo.QueueFamily = queueInfo.familyIndex;
            initInfo.Queue = queueInfo.queue;
            // 固定 16(IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE * 2)在只有字体图集/
            // 少数几个编辑窗口预览时够用, 但 AssetBrowser 缩略图会同时注册远超这个数量的纹理——
            // 池子耗尽时 vkAllocateDescriptorSets 返回 VK_NULL_HANDLE, registerTexture() 静默
            // 拿到 0, 表现为所有纹理预览(包括跟缩略图无关的)一起失效, 不是某处崩溃或报错。
            initInfo.DescriptorPoolSize = 1024;
            initInfo.MinImageCount = kMaxFramesInFlight;
            initInfo.ImageCount = kMaxFramesInFlight;
            initInfo.UseDynamicRendering = useDynamicRendering;

            if (useDynamicRendering)
            {
                initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.sType =
                    VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
                initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
                initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats =
                    &gVulkanColorFormatStorage;
            }
            else
            {
                initInfo.PipelineInfoMain.RenderPass =
                    vkDevice.getRenderPassCache().getOrCreateRenderPass(
                        { &gVulkanColorFormatStorage, 1 },
                        VK_FORMAT_UNDEFINED,
                        VK_SAMPLE_COUNT_1_BIT
                    );
            }

            gAvailable = ImGui_ImplVulkan_Init(&initInfo);
            if (gAvailable)
                initShadedImagePipelines(vkDevice, colorFormat);
        }
        else if (gBackend == rhi::Backend::WebGPU)
        {
            const auto& wgpuDevice = static_cast<const wgpu::Device&>(renderBackend.getDevice());

            ImGui_ImplWGPU_InitInfo initInfo{};
            initInfo.Device = wgpuDevice.getHandle();
            initInfo.NumFramesInFlight = kMaxFramesInFlight;
            initInfo.RenderTargetFormat = wgpu::convertTextureFormat(colorFormat);

            gAvailable = ImGui_ImplWGPU_Init(&initInfo);
            if (gAvailable)
                initShadedImagePipelinesWGPU(wgpuDevice.getHandle(), initInfo.RenderTargetFormat);
        }
    }

    void ImGuiBackend::shutdown()
    {
        if (gAvailable)
        {
            if (gBackend == rhi::Backend::Vulkan)
            {
                shutdownShadedImagePipelines();
                ImGui_ImplVulkan_Shutdown();
            }
            else
            {
                shutdownShadedImagePipelinesWGPU();
                ImGui_ImplWGPU_Shutdown();
            }
        }
        gAvailable = false;
    }

    void ImGuiBackend::renderDrawData(rhi::CommandList& cmd, ImDrawData* drawData)
    {
        if (!gAvailable || !drawData)
            return;

        // invokeNativeCallback() 按值接管这个临时 lambda, 把它 move 进自己的稳定存储
        // (CommandList::mNativeCallbacks), 调用方(这里)不需要再准备一份外部存储。
        cmd.invokeNativeCallback(
            [drawData](const rhi::NativePassHandle& handle)
            {
                if (handle.type == rhi::NativePassHandleType::Vulkan_Command_Buffer)
                {
                    ImGui_ImplVulkan_RenderDrawData(drawData, static_cast<VkCommandBuffer>(handle.handle));
                }
                else
                {
                    ImGui_ImplWGPU_RenderDrawData(drawData, static_cast<WGPURenderPassEncoder>(handle.handle));
                }
            }
        );
    }

    bool ImGuiBackend::isAvailable() noexcept
    {
        return gAvailable;
    }

    rhi::Backend ImGuiBackend::getBackend() noexcept
    {
        return gBackend;
    }

    ImTextureID ImGuiBackend::registerTexture(rhi::TextureView& view)
    {
        if (!gAvailable)
            return 0;

        if (gBackend == rhi::Backend::Vulkan)
        {
            const auto& vkView = static_cast<const vk::TextureView&>(view);
            VkDescriptorSet descriptorSet = ImGui_ImplVulkan_AddTexture(vkView.getHandle(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            return reinterpret_cast<ImTextureID>(descriptorSet);
        }
        else
        {
            // imgui_impl_wgpu 直接把 WGPUTextureView 本身当作 ImTextureID 使用, 不像 Vulkan
            // 后端那样需要额外注册一个描述符——见 imgui_impl_wgpu.h 顶部 "Use 'WGPUTextureView'
            // as ImTextureID" 的说明, 这里没有对应的 AddTexture() 可调。
            const auto& wgpuView = static_cast<const wgpu::TextureView&>(view);
            return reinterpret_cast<ImTextureID>(wgpuView.getHandle());
        }
    }

    void ImGuiBackend::unregisterTexture(ImTextureID id)
    {
        if (!gAvailable || !id)
            return;

        if (gBackend == rhi::Backend::WebGPU)
        {
            // WebGPU 侧 ImTextureID 只是 WGPUTextureView 的原始句柄, 没有独立的注册状态需要
            // 释放——底层 TextureView 自身的生命周期(以及它要不要延迟销毁)由调用方按引擎
            // 其它 GPU 资源同样的规则管理, 这里不用、也不应该重复处理。
            return;
        }

        // 此时可能还有尚未被渲染线程消费的 ImDrawData 快照引用着这个 VkDescriptorSet。
        // 这里的延迟不能只按 kMaxFramesInFlight(GPU 侧命令缓冲区完成所需的帧数)来算——
        // 逻辑线程本身可以领先渲染线程最多 RenderFrontend::kMaxQueuedFrames 帧(见
        // RenderFrontend.h 里的计数信号量), 也就是说在 unregisterTexture() 被渲染线程
        // 处理的这一刻, 逻辑线程可能已经又提前用同一个(马上要失效的)ImTextureID 画了
        // 好几帧、生成了好几份还没被渲染线程消费的 ImDrawData 快照——每一份后续都会各自
        // 产出一次引用这个 VkDescriptorSet 的命令缓冲区提交。只按 kMaxFramesInFlight 延迟
        // 在实测中(SceneView 编辑器视口)会在 vkFreeDescriptorSets 时报"仍被某个
        // VkCommandBuffer 使用中", 所以这里额外加上等价于 kMaxQueuedFrames 的余量, 覆盖
        // "逻辑线程领先读到旧 id" 和 "GPU 侧命令缓冲区尚未执行完" 这两段延迟的总和。
        gRenderBackend->getDeferredTaskQueue().push(
            [descriptorSet = reinterpret_cast<VkDescriptorSet>(id)]()
            {
                ImGui_ImplVulkan_RemoveTexture(descriptorSet);
            },
            kMaxFramesInFlight + kMaxQueuedFrames
        );
    }

    void ImGuiBackend::newFrame()
    {
        if (!gAvailable)
            return;

        if (gBackend == rhi::Backend::Vulkan)
            ImGui_ImplVulkan_NewFrame();
        else
            ImGui_ImplWGPU_NewFrame();
    }

    bool ImGuiBackend::isShadedImageAvailable() noexcept
    {
        return gShadedImageAvailable;
    }

    void ImGuiBackend::bindShadedImagePipeline(const ImDrawList*, const ImDrawCmd* cmd)
    {
        if (!gShadedImageAvailable)
            return;

        const auto* params = static_cast<const ShadedImageParams*>(cmd->UserCallbackData);

        if (gBackend == rhi::Backend::Vulkan)
        {
            auto* renderState = static_cast<ImGui_ImplVulkan_RenderState*>(ImGui::GetPlatformIO().Renderer_RenderState);
            if (!renderState)
                return;

            VkPipeline pipeline = VK_NULL_HANDLE;
            switch (params->mode)
            {
            case ShadedImageMode::Normal:
                pipeline = gNormalImagePipeline;
                break;
            case ShadedImageMode::Gray:
                pipeline = gGrayImagePipeline;
                break;
            case ShadedImageMode::Plain:
                pipeline = gPlainImagePipeline;
                break;
            case ShadedImageMode::SRGB:
                pipeline = gSRGBImagePipeline;
                break;
            case ShadedImageMode::Cube:
                pipeline = gCubeImagePipeline;
                break;
            }
            if (pipeline == VK_NULL_HANDLE)
                return;

            // channelMask 不走这里——它编进了紧跟着的那条 AddImage() 顶点颜色的 R 通道字节里
            // (纯顶点输入数据, 不受 pipeline layout 兼容性规则约束), 由各模式自己的 fragment
            // shader 解出来。这里只换 pipeline, 不碰 push constant/descriptor set, 保证跟 imgui
            // 自己的 pipeline layout 严格一致——多加任何一段 push constant range 都会让 imgui
            // 后续的 vkCmdBindDescriptorSets 被判定跟当前 pipeline layout 不兼容, 连累 set 0/1
            // 一起失效(实测触发过 VUID-vkCmdDrawIndexed-None-08600)。
            vkCmdBindPipeline(renderState->CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        }
        else
        {
            auto* renderState = static_cast<ImGui_ImplWGPU_RenderState*>(ImGui::GetPlatformIO().Renderer_RenderState);
            if (!renderState)
                return;

            WGPURenderPipeline pipeline = nullptr;
            switch (params->mode)
            {
            case ShadedImageMode::Normal:
                pipeline = gWgpuNormalImagePipeline;
                break;
            case ShadedImageMode::Gray:
                pipeline = gWgpuGrayImagePipeline;
                break;
            case ShadedImageMode::Plain:
                pipeline = gWgpuPlainImagePipeline;
                break;
            case ShadedImageMode::SRGB:
                pipeline = gWgpuSRGBImagePipeline;
                break;
            case ShadedImageMode::Cube:
                pipeline = gWgpuCubeImagePipeline;
                break;
            }
            if (!pipeline)
                return;

            wgpuRenderPassEncoderSetPipeline(renderState->RenderPassEncoder, pipeline);

            if (params->mode == ShadedImageMode::Cube)
            {
                // 紧跟着的 AddImage() 画图命令自己的 textureId 被 CubeImage() 故意传成了 0,
                // 用来绕开 imgui_impl_wgpu.cpp 给它自动建 bind group 那条写死 2D 的路径(见
                // ShadedImageParams::textureId 的注释)——group 1 只能靠这里自己绑, 真实的
                // WGPUTextureView 是 params->textureId。
                WGPUTextureView view = reinterpret_cast<WGPUTextureView>(params->textureId);
                WGPUBindGroup& bindGroup = gWgpuCubeBindGroupCache[view];
                if (!bindGroup)
                {
                    WGPUBindGroupEntry entry{};
                    entry.binding = 0;
                    entry.textureView = view;

                    WGPUBindGroupDescriptor desc{};
                    desc.layout = gWgpuCubeImageBindGroupLayout;
                    desc.entryCount = 1;
                    desc.entries = &entry;
                    bindGroup = wgpuDeviceCreateBindGroup(renderState->Device, &desc);
                }
                wgpuRenderPassEncoderSetBindGroup(renderState->RenderPassEncoder, 1, bindGroup, 0, nullptr);
            }
        }
    }
}
