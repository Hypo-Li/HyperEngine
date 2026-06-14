#pragma once
#include "VertexLayout.h"
#include "Shader.h"

#include <Core/Common/Hash.h>

namespace hyp
{
    static constexpr VkStencilOpState kDefaultStencilOpState = VkStencilOpState{
        VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_NEVER,
        0, 0, 0
    };

    class StencilState
    {
    public:
        virtual VkStencilOpState getFront() const noexcept = 0;

        virtual VkStencilOpState getBack() const noexcept = 0;

        template <VkStencilOpState Front = kDefaultStencilOpState, VkStencilOpState Back = kDefaultStencilOpState>
        static StencilState* get() noexcept;
    };

    template <VkStencilOpState Front, VkStencilOpState Back>
    class TStencilState : public StencilState
    {
    public:
        virtual VkStencilOpState getFront() const noexcept override
        {
            return Front;
        }

        virtual VkStencilOpState getBack() const noexcept override
        {
            return Back;
        }
    };

    template <VkStencilOpState Front, VkStencilOpState Back>
    StencilState* StencilState::get() noexcept
    {
        static TStencilState<Front, Back> stencilState;
        return &stencilState;
    }

    using AttachmentBlendState = VkPipelineColorBlendAttachmentState;
    static constexpr AttachmentBlendState kDefaultAttachmentBlendState = AttachmentBlendState{
        VK_FALSE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD,
        VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, vk::kColorComponentAll
    };

    class ColorBlendState
    {
    public:
        virtual AttachmentBlendState getAttachmentBlendState(uint8_t index) const noexcept = 0;

        template <
            AttachmentBlendState Attachment0 = kDefaultAttachmentBlendState,
            AttachmentBlendState Attachment1 = kDefaultAttachmentBlendState,
            AttachmentBlendState Attachment2 = kDefaultAttachmentBlendState,
            AttachmentBlendState Attachment3 = kDefaultAttachmentBlendState,
            AttachmentBlendState Attachment4 = kDefaultAttachmentBlendState,
            AttachmentBlendState Attachment5 = kDefaultAttachmentBlendState,
            AttachmentBlendState Attachment6 = kDefaultAttachmentBlendState,
            AttachmentBlendState Attachment7 = kDefaultAttachmentBlendState
        >
        static ColorBlendState* get() noexcept;
    };

    template <
        AttachmentBlendState Attachment0,
        AttachmentBlendState Attachment1,
        AttachmentBlendState Attachment2,
        AttachmentBlendState Attachment3,
        AttachmentBlendState Attachment4,
        AttachmentBlendState Attachment5,
        AttachmentBlendState Attachment6,
        AttachmentBlendState Attachment7
    >
    class TColorBlendState : public ColorBlendState
    {
    public:
        virtual AttachmentBlendState getAttachmentBlendState(uint8_t index) const noexcept override
        {
            switch (index)
            {
            case 0: return Attachment0;
            case 1: return Attachment1;
            case 2: return Attachment2;
            case 3: return Attachment3;
            case 4: return Attachment4;
            case 5: return Attachment5;
            case 6: return Attachment6;
            case 7: return Attachment7;
            default: return kDefaultAttachmentBlendState;
            }
        }
    };

    template <
        AttachmentBlendState Attachment0,
        AttachmentBlendState Attachment1,
        AttachmentBlendState Attachment2,
        AttachmentBlendState Attachment3,
        AttachmentBlendState Attachment4,
        AttachmentBlendState Attachment5,
        AttachmentBlendState Attachment6,
        AttachmentBlendState Attachment7
    >
    ColorBlendState* ColorBlendState::get() noexcept
    {
        static TColorBlendState <
            Attachment0,
            Attachment1,
            Attachment2,
            Attachment3,
            Attachment4,
            Attachment5,
            Attachment6,
            Attachment7
        > colorBlendState;
        return &colorBlendState;
    }

    class AttachmentFormats
    {
    public:
        virtual VkFormat getColorFormat(uint8_t index) const noexcept = 0;

        virtual VkFormat getDepthStencilFormat() const noexcept = 0;

        template <
            VkFormat DepthStencilFormat,
            VkFormat ColorFormat0 = VK_FORMAT_UNDEFINED,
            VkFormat ColorFormat1 = VK_FORMAT_UNDEFINED,
            VkFormat ColorFormat2 = VK_FORMAT_UNDEFINED,
            VkFormat ColorFormat3 = VK_FORMAT_UNDEFINED,
            VkFormat ColorFormat4 = VK_FORMAT_UNDEFINED,
            VkFormat ColorFormat5 = VK_FORMAT_UNDEFINED,
            VkFormat ColorFormat6 = VK_FORMAT_UNDEFINED,
            VkFormat ColorFormat7 = VK_FORMAT_UNDEFINED
        >
        static AttachmentFormats* get() noexcept;
    };

    template <
        VkFormat DepthStencilFormat,
        VkFormat ColorFormat0,
        VkFormat ColorFormat1,
        VkFormat ColorFormat2,
        VkFormat ColorFormat3,
        VkFormat ColorFormat4,
        VkFormat ColorFormat5,
        VkFormat ColorFormat6,
        VkFormat ColorFormat7
    >
    class TAttachmentFormats : public AttachmentFormats
    {
    public:
        virtual VkFormat getColorFormat(uint8_t index) const noexcept override
        {
            switch (index)
            {
            case 0: return ColorFormat0;
            case 1: return ColorFormat1;
            case 2: return ColorFormat2;
            case 3: return ColorFormat3;
            case 4: return ColorFormat4;
            case 5: return ColorFormat5;
            case 6: return ColorFormat6;
            case 7: return ColorFormat7;
            default:
                assert(false && "Color format out of range");
                return VK_FORMAT_UNDEFINED;
            }
        }

        virtual VkFormat getDepthStencilFormat() const noexcept override
        {
            return DepthStencilFormat;
        }
    };

    template <
        VkFormat DepthStencilFormat,
        VkFormat ColorFormat0,
        VkFormat ColorFormat1,
        VkFormat ColorFormat2,
        VkFormat ColorFormat3,
        VkFormat ColorFormat4,
        VkFormat ColorFormat5,
        VkFormat ColorFormat6,
        VkFormat ColorFormat7
    >
    static AttachmentFormats* AttachmentFormats::get() noexcept
    {
        static TAttachmentFormats <
            DepthStencilFormat,
            ColorFormat0,
            ColorFormat1,
            ColorFormat2,
            ColorFormat3,
            ColorFormat4,
            ColorFormat5,
            ColorFormat6,
            ColorFormat7
        > attachmentFormats;
        return &attachmentFormats;
    }

    enum class PolygonMode
    {
        Fill,
        Line,
        Point
    };

    enum class Topology
    {
        Point_List,
        Line_List,
        Line_Strip,
        Triangle_List,
        Triangle_Strip,
        Triangle_Fan,
    };

    enum class SampleCount
    {
        Count_1,
        Count_2,
        Count_4,
        Count_8,
    };

    enum class CullMode
    {
        None,
        Front,
        Back,
    };

    enum class FrontFace
    {
        Counter_Clockwise,
        Clockwise
    };

    enum class CompareOp
    {
        Never,
        Less,
        Equal,
        Less_Or_Equal,
        Greater,
        Not_Equal,
        Greater_Or_Equal,
        Always
    };

    struct GraphicsPipelineState
    {
        ShaderVariant shader;
        VertexLayout vertexLayout;
        uint32_t polygonMode : 2;
        uint32_t topology : 4;
        uint32_t samples : 2;
        uint32_t cullMode : 2;
        uint32_t frontFace : 1;
        uint32_t depthTestEnable : 1;
        uint32_t depthWriteEnable : 1;
        uint32_t depthCompareOp : 4;
        uint32_t stencilTestEnable : 1;
        StencilState* stencilState;
        ColorBlendState* colorBlendState;
        AttachmentFormats* attachmentFormats;

        bool operator==(const GraphicsPipelineState& other) const noexcept
        {
            return shader == other.shader &&
                vertexLayout == other.vertexLayout &&
                polygonMode == other.polygonMode &&
                topology == other.topology &&
                samples == other.samples &&
                cullMode == other.cullMode &&
                frontFace == other.frontFace &&
                depthTestEnable == other.depthTestEnable &&
                stencilTestEnable == other.stencilTestEnable &&
                stencilState == other.stencilState &&
                colorBlendState == other.colorBlendState &&
                attachmentFormats == other.attachmentFormats;
        }
    };
}

namespace std
{
    template <>
    struct hash<hyp::GraphicsPipelineState>
    {
        size_t operator()(const hyp::GraphicsPipelineState& state) const noexcept
        {
            size_t seed = 0;
            hyp::Hash::combine(seed, state.shader);
            hyp::Hash::combine(seed, state.vertexLayout);
            hyp::Hash::combine(seed, state.polygonMode);
            hyp::Hash::combine(seed, state.topology);
            hyp::Hash::combine(seed, state.samples);
            hyp::Hash::combine(seed, state.cullMode);
            hyp::Hash::combine(seed, state.frontFace);
            hyp::Hash::combine(seed, state.depthTestEnable);
            hyp::Hash::combine(seed, state.stencilTestEnable);
            hyp::Hash::combine(seed, state.stencilState);
            hyp::Hash::combine(seed, state.colorBlendState);
            hyp::Hash::combine(seed, state.attachmentFormats);
            return seed;
        }
    };
}
