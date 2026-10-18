#pragma once
#include "VertexLayout.h"
#include "Shader.h"

#include <Core/Common/Hash.h>

namespace hyp
{
    static constexpr rhi::StencilOpState kDefaultStencilOpState = rhi::StencilOpState{
        rhi::StencilOp::Keep,
        rhi::StencilOp::Keep,
        rhi::StencilOp::Keep,
        rhi::CompareOp::Always,
        0,
        0
    };

    class StencilState
    {
    public:
        virtual rhi::StencilOpState getFront() const noexcept = 0;

        virtual rhi::StencilOpState getBack() const noexcept = 0;

        template <rhi::StencilOpState Front = kDefaultStencilOpState, rhi::StencilOpState Back = kDefaultStencilOpState>
        static StencilState* get() noexcept;
    };

    template <rhi::StencilOpState Front, rhi::StencilOpState Back>
    class TStencilState : public StencilState
    {
    public:
        virtual rhi::StencilOpState getFront() const noexcept override
        {
            return Front;
        }

        virtual rhi::StencilOpState getBack() const noexcept override
        {
            return Back;
        }
    };

    template <rhi::StencilOpState Front, rhi::StencilOpState Back>
    StencilState* StencilState::get() noexcept
    {
        static TStencilState<Front, Back> stencilState;
        return &stencilState;
    }

    static constexpr rhi::ColorBlendAttachmentState kDefaultColorBlendAttachmentState = rhi::ColorBlendAttachmentState{
        false, rhi::BlendFactor::One, rhi::BlendFactor::Zero, rhi::BlendOp::Add,
        rhi::BlendFactor::One, rhi::BlendFactor::Zero, rhi::BlendOp::Add, 0xFF
    };

    class ColorBlendState
    {
    public:
        virtual rhi::ColorBlendAttachmentState getAttachmentBlendState(uint8_t index) const noexcept = 0;

        template <
            rhi::ColorBlendAttachmentState Attachment0 = kDefaultColorBlendAttachmentState,
            rhi::ColorBlendAttachmentState Attachment1 = kDefaultColorBlendAttachmentState,
            rhi::ColorBlendAttachmentState Attachment2 = kDefaultColorBlendAttachmentState,
            rhi::ColorBlendAttachmentState Attachment3 = kDefaultColorBlendAttachmentState,
            rhi::ColorBlendAttachmentState Attachment4 = kDefaultColorBlendAttachmentState,
            rhi::ColorBlendAttachmentState Attachment5 = kDefaultColorBlendAttachmentState,
            rhi::ColorBlendAttachmentState Attachment6 = kDefaultColorBlendAttachmentState,
            rhi::ColorBlendAttachmentState Attachment7 = kDefaultColorBlendAttachmentState
        >
        static ColorBlendState* get() noexcept;
    };

    template <
        rhi::ColorBlendAttachmentState Attachment0,
        rhi::ColorBlendAttachmentState Attachment1,
        rhi::ColorBlendAttachmentState Attachment2,
        rhi::ColorBlendAttachmentState Attachment3,
        rhi::ColorBlendAttachmentState Attachment4,
        rhi::ColorBlendAttachmentState Attachment5,
        rhi::ColorBlendAttachmentState Attachment6,
        rhi::ColorBlendAttachmentState Attachment7
    >
    class TColorBlendState : public ColorBlendState
    {
    public:
        virtual rhi::ColorBlendAttachmentState getAttachmentBlendState(uint8_t index) const noexcept override
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
            default: return kDefaultColorBlendAttachmentState;
            }
        }
    };

    template <
        rhi::ColorBlendAttachmentState Attachment0,
        rhi::ColorBlendAttachmentState Attachment1,
        rhi::ColorBlendAttachmentState Attachment2,
        rhi::ColorBlendAttachmentState Attachment3,
        rhi::ColorBlendAttachmentState Attachment4,
        rhi::ColorBlendAttachmentState Attachment5,
        rhi::ColorBlendAttachmentState Attachment6,
        rhi::ColorBlendAttachmentState Attachment7
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
        virtual rhi::TextureFormat getColorFormat(uint8_t index) const noexcept = 0;

        virtual rhi::TextureFormat getDepthStencilFormat() const noexcept = 0;

        template <
            rhi::TextureFormat DepthStencilFormat,
            rhi::TextureFormat ColorFormat0 = rhi::TextureFormat::Undefined,
            rhi::TextureFormat ColorFormat1 = rhi::TextureFormat::Undefined,
            rhi::TextureFormat ColorFormat2 = rhi::TextureFormat::Undefined,
            rhi::TextureFormat ColorFormat3 = rhi::TextureFormat::Undefined,
            rhi::TextureFormat ColorFormat4 = rhi::TextureFormat::Undefined,
            rhi::TextureFormat ColorFormat5 = rhi::TextureFormat::Undefined,
            rhi::TextureFormat ColorFormat6 = rhi::TextureFormat::Undefined,
            rhi::TextureFormat ColorFormat7 = rhi::TextureFormat::Undefined
        >
        static AttachmentFormats* get() noexcept;
    };

    template <
        rhi::TextureFormat DepthStencilFormat,
        rhi::TextureFormat ColorFormat0,
        rhi::TextureFormat ColorFormat1,
        rhi::TextureFormat ColorFormat2,
        rhi::TextureFormat ColorFormat3,
        rhi::TextureFormat ColorFormat4,
        rhi::TextureFormat ColorFormat5,
        rhi::TextureFormat ColorFormat6,
        rhi::TextureFormat ColorFormat7
    >
    class TAttachmentFormats : public AttachmentFormats
    {
    public:
        virtual rhi::TextureFormat getColorFormat(uint8_t index) const noexcept override
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
                return rhi::TextureFormat::Undefined;
            }
        }

        virtual rhi::TextureFormat getDepthStencilFormat() const noexcept override
        {
            return DepthStencilFormat;
        }
    };

    template <
        rhi::TextureFormat DepthStencilFormat,
        rhi::TextureFormat ColorFormat0,
        rhi::TextureFormat ColorFormat1,
        rhi::TextureFormat ColorFormat2,
        rhi::TextureFormat ColorFormat3,
        rhi::TextureFormat ColorFormat4,
        rhi::TextureFormat ColorFormat5,
        rhi::TextureFormat ColorFormat6,
        rhi::TextureFormat ColorFormat7
    >
    AttachmentFormats* AttachmentFormats::get() noexcept
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

    struct GraphicsPipelineState
    {
        ShaderVariant shader;
        VertexLayout vertexLayout;
        uint32_t topology : 4;
        uint32_t samples : 2;
        uint32_t cullMode : 2;
        uint32_t frontFace : 1;
        uint32_t depthTestEnable : 1;
        uint32_t depthWriteEnable : 1;
        uint32_t depthCompareOp : 4;
        uint32_t stencilTestEnable : 1;
        uint32_t depthClampEnable : 1;
        StencilState* stencilState;
        ColorBlendState* colorBlendState;
        AttachmentFormats* attachmentFormats;

        GraphicsPipelineState()
        {
            std::memset(this, 0, sizeof(GraphicsPipelineState));
            shader = ShaderVariant{};
            vertexLayout = VertexLayout{};
        }

        bool operator==(const GraphicsPipelineState& other) const noexcept
        {
            return
                shader == other.shader &&
                vertexLayout == other.vertexLayout &&
                topology == other.topology &&
                samples == other.samples &&
                cullMode == other.cullMode &&
                frontFace == other.frontFace &&
                depthTestEnable == other.depthTestEnable &&
                depthWriteEnable == other.depthWriteEnable &&
                depthCompareOp == other.depthCompareOp &&
                stencilTestEnable == other.stencilTestEnable &&
                depthClampEnable == other.depthClampEnable &&
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
            hyp::Hash::combine(seed, state.topology);
            hyp::Hash::combine(seed, state.samples);
            hyp::Hash::combine(seed, state.cullMode);
            hyp::Hash::combine(seed, state.frontFace);
            hyp::Hash::combine(seed, state.depthTestEnable);
            hyp::Hash::combine(seed, state.depthWriteEnable);
            hyp::Hash::combine(seed, state.depthCompareOp);
            hyp::Hash::combine(seed, state.stencilTestEnable);
            hyp::Hash::combine(seed, state.depthClampEnable);
            hyp::Hash::combine(seed, state.stencilState);
            hyp::Hash::combine(seed, state.colorBlendState);
            hyp::Hash::combine(seed, state.attachmentFormats);
            return seed;
        }
    };
}
