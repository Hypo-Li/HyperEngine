#pragma once
#include "Device.h"

namespace hyp::vk
{
    class ShaderModule : public Refable
    {
    public:
        ShaderModule(
            const Device& device,
            std::span<const uint8_t> code
        );

        ShaderModule(
            const Device& device,
            std::span<const uint32_t> code
        );

        ~ShaderModule();

        VkShaderModule getHandle() const noexcept
        {
            return mShaderModule;
        }

    private:
        const Device& mDevice;
        VkShaderModule mShaderModule{ VK_NULL_HANDLE };
    };

    class PipelineCache : public Refable
    {
    public:
        PipelineCache(
            const Device& device,
            std::span<const uint8_t> initialData
        );
        ~PipelineCache();

        VkPipelineCache getHandle() const noexcept
        {
            return mPipelineCache;
        }

        std::vector<uint8_t> getData() const;

    private:
        const Device& mDevice;
        VkPipelineCache mPipelineCache{ VK_NULL_HANDLE };
    };

    class PipelineLayout : public Refable
    {
    public:
        PipelineLayout(
            const Device& device,
            std::span<const VkDescriptorSetLayout> setLayouts,
            std::span<const VkPushConstantRange> pushConstantRanges
        );
        ~PipelineLayout();

        VkPipelineLayout getHandle() const noexcept
        {
            return mPipelineLayout;
        }

    private:
        const Device& mDevice;
        VkPipelineLayout mPipelineLayout{ VK_NULL_HANDLE };
    };

    static constexpr VkColorComponentFlags kColorComponentAll = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    static constexpr VkColorComponentFlags kColorComponentRGB = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;

    class Pipeline : public Refable
    {
    public:
        struct ShaderStage
        {
            VkShaderStageFlagBits stage;
            VkShaderModule module;
            const char* name = "main";
        };

        struct VertexInputState
        {
            std::span<const VkVertexInputBindingDescription> bindings;
            std::span<const VkVertexInputAttributeDescription> attributes;
        };

        struct InputAssemblyState
        {
            VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkBool32 primitiveRestartEnable = VK_FALSE;
        };

        struct TessellationState
        {
            uint32_t patchControlPoints;
        };

        struct ViewportState
        {
            std::span<const VkViewport> viewports;
            std::span<const VkRect2D> scissors;
        };

        struct RasterizationState
        {
            VkBool32 depthClampEnable = VK_FALSE;
            VkBool32 rasterizerDiscardEnable = VK_FALSE;
            VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
            VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
            VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            VkBool32 depthBiasEnable = VK_FALSE;
            float depthBiasConstantFactor = 0.0f;
            float depthBiasClamp = 0.0f;
            float depthBiasSlopeFactor = 0.0f;
            float lineWidth = 1.0f;
        };

        struct MultisampleState
        {
            VkSampleCountFlagBits rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            VkBool32 sampleShadingEnable = VK_FALSE;
            float minSampleShading;
            VkBool32 alphaToCoverageEnable = VK_FALSE;
            VkBool32 alphaToOneEnable = VK_FALSE;
        };

        struct DepthStencilState
        {
            VkBool32 depthTestEnable = VK_TRUE;
            VkBool32 depthWriteEnable = VK_TRUE;
            VkCompareOp depthCompareOp = VK_COMPARE_OP_GREATER;
            VkBool32 depthBoundsTestEnable = VK_FALSE;
            VkBool32 stencilTestEnable = VK_FALSE;
            VkStencilOpState front = {};
            VkStencilOpState back = {};
            float minDepthBounds;
            float maxDepthBounds;
        };

        struct ColorBlendState
        {
            VkBool32 logicOpEnable = VK_FALSE;
            VkLogicOp logicOp;
            std::span<const VkPipelineColorBlendAttachmentState> attachments;
            float blendConstants[4];
        };

        struct DynamicState
        {
            std::span<const VkDynamicState> dynamicStates;
        };

        struct GraphicsCreateInfo
        {
            std::span<const ShaderStage> shaderStages;
            VertexInputState vertexInputState;
            InputAssemblyState inputAssemblyState;
            TessellationState tessellationState;
            ViewportState viewportState;
            RasterizationState rasterizationState;
            MultisampleState multisampleState;
            DepthStencilState depthStencilState;
            ColorBlendState colorBlendState;
            DynamicState dynamicState;
            TRef<PipelineLayout> layout;
            std::span<const VkFormat> colorFormats;
            VkFormat depthStencilFormat = VK_FORMAT_UNDEFINED;
            uint32_t subpass = 0;
            TRef<Pipeline> basePipeline;
        };

        struct ComputeCreateInfo
        {
            ShaderStage shaderStage;
            TRef<PipelineLayout> layout;
            TRef<Pipeline> basePipeline;
        };

        struct ShaderGroup
        {
            VkRayTracingShaderGroupTypeKHR type;
            uint32_t generalShader = VK_SHADER_UNUSED_KHR;
            uint32_t closestHitShader = VK_SHADER_UNUSED_KHR;
            uint32_t anyHitShader = VK_SHADER_UNUSED_KHR;
            uint32_t intersectionShader = VK_SHADER_UNUSED_KHR;
        };

        struct RayTracingCreateInfo
        {
            std::span<const ShaderStage> shaderStages;
            std::span<const ShaderGroup> shaderGroups;
            uint32_t maxPipelineRayRecursionDepth;
            TRef<PipelineLayout> layout;
            TRef<Pipeline> basePipeline;
        };

        Pipeline(const Device& device, const GraphicsCreateInfo& createInfo, VkPipelineCache pipelineCache = VK_NULL_HANDLE);

        Pipeline(const Device& device, const ComputeCreateInfo& createInfo, VkPipelineCache pipelineCache = VK_NULL_HANDLE);

        Pipeline(const Device& device, const RayTracingCreateInfo& createInfo, VkPipelineCache pipelineCache = VK_NULL_HANDLE);

        ~Pipeline();

        VkPipeline getHandle() const noexcept
        {
            return mPipeline;
        }

        PipelineLayout& getLayout() const noexcept
        {
            return *mLayout;
        }

        inline VkPipelineBindPoint getBindPoint() const noexcept
        {
            return mBindPoint;
        }

        static std::vector<Pipeline*> createPipelines(const Device& device, std::span<const GraphicsCreateInfo> createInfos, VkPipelineCache pipelineCache = VK_NULL_HANDLE);

        static std::vector<Pipeline*> createPipelines(const Device& device, std::span<const ComputeCreateInfo> createInfos, VkPipelineCache pipelineCache = VK_NULL_HANDLE);

    private:
        Pipeline(const Device& device, PipelineLayout* layout, VkPipeline pipeline, VkPipelineBindPoint bindPoint);

    private:
        const Device& mDevice;
        TRef<PipelineLayout> mLayout;
        VkPipeline mPipeline{ VK_NULL_HANDLE };
        VkPipelineBindPoint mBindPoint;
    };
}
