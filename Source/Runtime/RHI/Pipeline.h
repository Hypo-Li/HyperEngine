#pragma once
#include "BindGroup.h"
#include "Texture.h"
#include "Sampler.h"

#include <array>

namespace hyp::rhi
{
    // Vulkan push constants / WebGPU immediates 的固定容量,每个 PipelineLayout 无条件预留,
    // 不需要按需声明——两边都保证这个大小内的开销可以忽略不计
    static constexpr uint32_t kImmediateDataSize = 64;

    struct PipelineLayoutDesc
    {
        std::array<const BindGroupLayout*, 4> bindGroupLayouts = { nullptr };
    };

    class PipelineLayout : public Refable
    {
    public:
        PipelineLayout(const Device& device) :
            mDevice(device)
        {
        }

        virtual ~PipelineLayout() = default;

        const Device& getDevice() const noexcept
        {
            return mDevice;
        }

    private:
        const Device& mDevice;
    };

    enum class PipelineBindPoint
    {
        Graphics,
        Compute,
    };

    enum class PipelineType
    {
        Graphics,
        Compute,
    };

    enum class VertexInputRate
    {
        Vertex,
        Instance
    };

    struct VertexInputBinding
    {
        uint32_t binding;
        uint32_t stride;
        VertexInputRate inputRate;
    };

    enum class VertexFormat
    {
        Float16,
        Float16x2,
        Float16x3,
        Float16x4,

        Float32,
        Float32x2,
        Float32x3,
        Float32x4,

        Uint32,
        Uint32x2,
        Uint32x3,
        Uint32x4,

        Sint32,
        Sint32x2,
        Sint32x3,
        Sint32x4,
    };

    struct VertexInputAttribute
    {
        uint32_t location;
        uint32_t binding;
        VertexFormat format;
        uint32_t offset;
    };

    struct VertexInputState
    {
        std::span<const VertexInputBinding> bindings;
        std::span<const VertexInputAttribute> attributes;
    };

    enum class PrimitiveTopology
    {
        Point_List,
        Line_List,
        Line_Strip,
        Triangle_List,
        Triangle_Strip,
    };

    struct InputAssemblyState
    {
        PrimitiveTopology topology = PrimitiveTopology::Triangle_List;
    };

    enum class CullMode : uint8_t
    {
        None,
        Front,
        Back,
        Front_And_Back,
    };

    enum class FrontFace : uint8_t
    {
        Clockwise,
        Counter_Clockwise,
    };

    struct RasterizationState
    {
        bool depthClampEnable = false;
        CullMode cullMode = CullMode::Back;
        FrontFace frontFace = FrontFace::Counter_Clockwise;
        bool depthBiasEnable = false;
        float depthBiasConstantFactor = 0.0f;
        float depthBiasClamp = 0.0f;
        float depthBiasSlopeFactor = 0.0f;
    };

    struct MultisampleState
    {
        uint8_t sampleCount = 1;
        bool alphaToCoverageEnable = false;
    };

    enum class StencilOp : uint8_t
    {
        Keep,
        Zero,
        Replace,
        Increment_And_Clamp,
        Decrement_And_Clamp,
        Invert,
        Increment_And_Wrap,
        Decrement_And_Wrap
    };

    struct StencilOpState
    {
        StencilOp failOp = StencilOp::Keep;
        StencilOp passOp = StencilOp::Keep;
        StencilOp depthFailOp = StencilOp::Keep;
        CompareOp compareOp = CompareOp::Always;
        uint32_t compareMask = 0xFFFFFFFF;
        uint32_t writeMask = 0xFFFFFFFF;
    };

    struct DepthStencilState
    {
        bool depthTestEnable = false;
        bool depthWriteEnable = false;
        CompareOp depthCompareOp = CompareOp::Greater;
        bool stencilTestEnable = false;
        StencilOpState front = {};
        StencilOpState back = {};
    };

    enum class BlendFactor : uint8_t
    {
        Zero,
        One,
        Src_Color,
        One_Minus_Src_Color,
        Dst_Color,
        One_Minus_Dst_Color,
        Src_Alpha,
        One_Minus_Src_Alpha,
        Dst_Alpha,
        One_Minus_Dst_Alpha,
        Constant_Color,
        One_Minus_Constant_Color,
        Constant_Alpha,
        One_Minus_Constant_Alpha,
        Src_Alpha_Saturate,
        Src1_Color,
        One_Minus_Src1_Color,
        Src1_Alpha,
    };

    enum class BlendOp : uint8_t
    {
        Add,
        Subtract,
        Reverse_Subtract,
        Min,
        Max
    };

    struct ColorBlendAttachmentState
    {
        bool blendEnable = false;
        BlendFactor srcColorBlendFactor = BlendFactor::One;
        BlendFactor dstColorBlendFactor = BlendFactor::Zero;
        BlendOp colorBlendOp = BlendOp::Add;
        BlendFactor srcAlphaBlendFactor = BlendFactor::One;
        BlendFactor dstAlphaBlendFactor = BlendFactor::Zero;
        BlendOp alphaBlendOp = BlendOp::Add;
        uint8_t colorWriteMask = 0xFF;
    };

    struct ColorBlendState
    {
        std::span<const ColorBlendAttachmentState> attachments;
    };

    // Viewport, Scissor, Blend Constants, Stencil Reference is always dynamic
    struct GraphicsPipelineDesc
    {
        std::span<const ShaderModule* const> shaderStages;
        VertexInputState vertexInputState;
        InputAssemblyState inputAssemblyState;
        RasterizationState rasterizationState;
        MultisampleState multisampleState;
        DepthStencilState depthStencilState;
        ColorBlendState colorBlendState;
        std::span<const TextureFormat> colorFormats;
        TextureFormat depthStencilFormat = TextureFormat::Undefined;
        const PipelineLayout* pipelineLayout;
    };

    struct ComputePipelineDesc
    {
        const ShaderModule* shaderModule;
        const PipelineLayout* pipelineLayout;
    };

    class Pipeline : public Refable
    {
    public:
        Pipeline(const Device& device, const PipelineLayout& layout, PipelineType type) :
            mDevice(device),
            mLayout(layout),
            mType(type)
        {
        }

        virtual ~Pipeline() = default;

        const Device& getDevice() const noexcept
        {
            return mDevice;
        }

        const PipelineLayout& getLayout() const noexcept
        {
            return mLayout;
        }

        PipelineType getType() const noexcept
        {
            return mType;
        }

    private:
        const Device& mDevice;
        const PipelineLayout& mLayout;
        PipelineType mType;
    };
}
