#include "Pipeline.h"
#include "BindGroup.h"
#include "ShaderModule.h"
#include "WgpuFormatUtil.h"

#include <Core/Common/LogSystem.h>

#include <array>
#include <vector>

namespace hyp::wgpu
{
    static WGPUShaderStage convertShaderStage(rhi::ShaderStage stage) noexcept
    {
        switch (stage)
        {
        case rhi::ShaderStage::Vertex:   return WGPUShaderStage_Vertex;
        case rhi::ShaderStage::Fragment: return WGPUShaderStage_Fragment;
        case rhi::ShaderStage::Compute:  return WGPUShaderStage_Compute;
        default:                         return WGPUShaderStage_None;
        }
    }

    static WGPUVertexFormat convertVertexFormat(rhi::VertexFormat format) noexcept
    {
        switch (format)
        {
        case rhi::VertexFormat::Float16:
            assert(false && "WebGPU does not support 1-component float16 vertex format");
            return WGPUVertexFormat_Float32;
        case rhi::VertexFormat::Float16x2:  return WGPUVertexFormat_Float16x2;
        case rhi::VertexFormat::Float16x3:
            assert(false && "WebGPU does not support float16x3 vertex format");
            return WGPUVertexFormat_Float16x4;
        case rhi::VertexFormat::Float16x4:  return WGPUVertexFormat_Float16x4;
        case rhi::VertexFormat::Float32:    return WGPUVertexFormat_Float32;
        case rhi::VertexFormat::Float32x2:  return WGPUVertexFormat_Float32x2;
        case rhi::VertexFormat::Float32x3:  return WGPUVertexFormat_Float32x3;
        case rhi::VertexFormat::Float32x4:  return WGPUVertexFormat_Float32x4;
        case rhi::VertexFormat::Uint32:     return WGPUVertexFormat_Uint32;
        case rhi::VertexFormat::Uint32x2:   return WGPUVertexFormat_Uint32x2;
        case rhi::VertexFormat::Uint32x3:
            assert(false && "WebGPU does not support uint32x3 vertex format");
            return WGPUVertexFormat_Uint32x4;
        case rhi::VertexFormat::Uint32x4:   return WGPUVertexFormat_Uint32x4;
        case rhi::VertexFormat::Sint32:     return WGPUVertexFormat_Sint32;
        case rhi::VertexFormat::Sint32x2:   return WGPUVertexFormat_Sint32x2;
        case rhi::VertexFormat::Sint32x3:
            assert(false && "WebGPU does not support sint32x3 vertex format");
            return WGPUVertexFormat_Sint32x4;
        case rhi::VertexFormat::Sint32x4:   return WGPUVertexFormat_Sint32x4;
        default:
            assert(false && "Unsupported vertex format");
            return WGPUVertexFormat_Float32;
        }
    }

    static WGPUVertexStepMode convertVertexStepMode(rhi::VertexInputRate rate) noexcept
    {
        switch (rate)
        {
        case rhi::VertexInputRate::Instance: return WGPUVertexStepMode_Instance;
        default:                             return WGPUVertexStepMode_Vertex;
        }
    }

    static WGPUPrimitiveTopology convertPrimitiveTopology(rhi::PrimitiveTopology topology) noexcept
    {
        switch (topology)
        {
        case rhi::PrimitiveTopology::Point_List:     return WGPUPrimitiveTopology_PointList;
        case rhi::PrimitiveTopology::Line_List:      return WGPUPrimitiveTopology_LineList;
        case rhi::PrimitiveTopology::Line_Strip:     return WGPUPrimitiveTopology_LineStrip;
        case rhi::PrimitiveTopology::Triangle_List:  return WGPUPrimitiveTopology_TriangleList;
        case rhi::PrimitiveTopology::Triangle_Strip: return WGPUPrimitiveTopology_TriangleStrip;
        default:
            assert(false && "Unsupported primitive topology");
            return WGPUPrimitiveTopology_TriangleList;
        }
    }

    static WGPUFrontFace convertFrontFace(rhi::FrontFace face) noexcept
    {
        switch (face)
        {
        case rhi::FrontFace::Clockwise: return WGPUFrontFace_CW;
        default:                        return WGPUFrontFace_CCW;
        }
    }

    static WGPUCullMode convertCullMode(rhi::CullMode mode) noexcept
    {
        switch (mode)
        {
        case rhi::CullMode::Front:          return WGPUCullMode_Front;
        case rhi::CullMode::Back:           return WGPUCullMode_Back;
        case rhi::CullMode::Front_And_Back: return WGPUCullMode_Front;
        default:                            return WGPUCullMode_None;
        }
    }

    static WGPUCompareFunction convertCompareFunction(rhi::CompareOp op) noexcept
    {
        switch (op)
        {
        case rhi::CompareOp::Never:         return WGPUCompareFunction_Never;
        case rhi::CompareOp::Less:          return WGPUCompareFunction_Less;
        case rhi::CompareOp::Equal:         return WGPUCompareFunction_Equal;
        case rhi::CompareOp::Less_Equal:    return WGPUCompareFunction_LessEqual;
        case rhi::CompareOp::Greater:       return WGPUCompareFunction_Greater;
        case rhi::CompareOp::Not_Equal:     return WGPUCompareFunction_NotEqual;
        case rhi::CompareOp::Greater_Equal: return WGPUCompareFunction_GreaterEqual;
        case rhi::CompareOp::Always:        return WGPUCompareFunction_Always;
        default:                            return WGPUCompareFunction_Always;
        }
    }

    static WGPUStencilOperation convertStencilOperation(rhi::StencilOp op) noexcept
    {
        switch (op)
        {
        case rhi::StencilOp::Keep:                return WGPUStencilOperation_Keep;
        case rhi::StencilOp::Zero:                return WGPUStencilOperation_Zero;
        case rhi::StencilOp::Replace:             return WGPUStencilOperation_Replace;
        case rhi::StencilOp::Increment_And_Clamp: return WGPUStencilOperation_IncrementClamp;
        case rhi::StencilOp::Decrement_And_Clamp: return WGPUStencilOperation_DecrementClamp;
        case rhi::StencilOp::Invert:              return WGPUStencilOperation_Invert;
        case rhi::StencilOp::Increment_And_Wrap:  return WGPUStencilOperation_IncrementWrap;
        case rhi::StencilOp::Decrement_And_Wrap:  return WGPUStencilOperation_DecrementWrap;
        default:                                  return WGPUStencilOperation_Keep;
        }
    }

    static WGPUBlendFactor convertBlendFactor(rhi::BlendFactor factor) noexcept
    {
        switch (factor)
        {
        case rhi::BlendFactor::Zero:                     return WGPUBlendFactor_Zero;
        case rhi::BlendFactor::One:                      return WGPUBlendFactor_One;
        case rhi::BlendFactor::Src_Color:                return WGPUBlendFactor_Src;
        case rhi::BlendFactor::One_Minus_Src_Color:      return WGPUBlendFactor_OneMinusSrc;
        case rhi::BlendFactor::Dst_Color:                return WGPUBlendFactor_Dst;
        case rhi::BlendFactor::One_Minus_Dst_Color:      return WGPUBlendFactor_OneMinusDst;
        case rhi::BlendFactor::Src_Alpha:                return WGPUBlendFactor_SrcAlpha;
        case rhi::BlendFactor::One_Minus_Src_Alpha:      return WGPUBlendFactor_OneMinusSrcAlpha;
        case rhi::BlendFactor::Dst_Alpha:                return WGPUBlendFactor_DstAlpha;
        case rhi::BlendFactor::One_Minus_Dst_Alpha:      return WGPUBlendFactor_OneMinusDstAlpha;
        case rhi::BlendFactor::Constant_Color:           return WGPUBlendFactor_Constant;
        case rhi::BlendFactor::One_Minus_Constant_Color: return WGPUBlendFactor_OneMinusConstant;
        case rhi::BlendFactor::Constant_Alpha:           return WGPUBlendFactor_Constant;
        case rhi::BlendFactor::One_Minus_Constant_Alpha: return WGPUBlendFactor_OneMinusConstant;
        case rhi::BlendFactor::Src_Alpha_Saturate:       return WGPUBlendFactor_SrcAlphaSaturated;
        case rhi::BlendFactor::Src1_Color:               return WGPUBlendFactor_Src1;
        case rhi::BlendFactor::One_Minus_Src1_Color:     return WGPUBlendFactor_OneMinusSrc1;
        case rhi::BlendFactor::Src1_Alpha:               return WGPUBlendFactor_Src1Alpha;
        default:                                         return WGPUBlendFactor_One;
        }
    }

    static WGPUBlendOperation convertBlendOperation(rhi::BlendOp op) noexcept
    {
        switch (op)
        {
        case rhi::BlendOp::Add:              return WGPUBlendOperation_Add;
        case rhi::BlendOp::Subtract:         return WGPUBlendOperation_Subtract;
        case rhi::BlendOp::Reverse_Subtract: return WGPUBlendOperation_ReverseSubtract;
        case rhi::BlendOp::Min:              return WGPUBlendOperation_Min;
        case rhi::BlendOp::Max:              return WGPUBlendOperation_Max;
        default:                             return WGPUBlendOperation_Add;
        }
    }

    static WGPUColorWriteMask convertColorWriteMask(uint8_t mask) noexcept
    {
        WGPUColorWriteMask result = WGPUColorWriteMask_None;
        if (mask & 0x1) result |= WGPUColorWriteMask_Red;
        if (mask & 0x2) result |= WGPUColorWriteMask_Green;
        if (mask & 0x4) result |= WGPUColorWriteMask_Blue;
        if (mask & 0x8) result |= WGPUColorWriteMask_Alpha;
        return result;
    }

    PipelineLayout::PipelineLayout(const Device& device, const rhi::PipelineLayoutDesc& desc)
        : rhi::PipelineLayout(device)
    {
        uint32_t layoutCount = 0;
        std::array<WGPUBindGroupLayout, 4> layouts = { nullptr, nullptr, nullptr, nullptr };

        for (uint32_t i = 0; i < 4; ++i)
        {
            if (desc.bindGroupLayouts[i])
            {
                layouts[i] = static_cast<const BindGroupLayout*>(desc.bindGroupLayouts[i])->getHandle();
                layoutCount = i + 1;
            }
        }

        WGPUPipelineLayoutDescriptor layoutDesc = {};
        layoutDesc.nextInChain = nullptr;
        layoutDesc.label = WGPUStringView{ "UnnamedPipelineLayout", WGPU_STRLEN };
        layoutDesc.bindGroupLayoutCount = layoutCount;
        layoutDesc.bindGroupLayouts = layouts.data();
        layoutDesc.immediateSize = rhi::kImmediateDataSize;
        mLayout = wgpuDeviceCreatePipelineLayout(device.getHandle(), &layoutDesc);
    }

    PipelineLayout::~PipelineLayout()
    {
        if (mLayout)
        {
            wgpuPipelineLayoutRelease(mLayout);
            mLayout = nullptr;
        }
    }

    Pipeline::Pipeline(const Device& device, const rhi::GraphicsPipelineDesc& desc)
        : rhi::Pipeline(device, *desc.pipelineLayout, rhi::PipelineType::Graphics)
    {
        WGPUDevice wgpuDevice = device.getHandle();
        WGPUPipelineLayout layout = static_cast<const PipelineLayout*>(desc.pipelineLayout)->getHandle();

        const rhi::ShaderModule* vertexShader = nullptr;
        const rhi::ShaderModule* fragmentShader = nullptr;
        for (const rhi::ShaderModule* shader : desc.shaderStages)
        {
            switch (shader->getStage())
            {
            case rhi::ShaderStage::Vertex:
                vertexShader = shader;
                break;
            case rhi::ShaderStage::Fragment:
                fragmentShader = shader;
                break;
            default:
                break;
            }
        }

        std::vector<WGPUVertexBufferLayout> vertexBuffers;
        std::vector<std::vector<WGPUVertexAttribute>> attributesPerBinding;

        for (const auto& binding : desc.vertexInputState.bindings)
        {
            WGPUVertexBufferLayout bufferLayout = {};
            bufferLayout.arrayStride = binding.stride;
            bufferLayout.stepMode = convertVertexStepMode(binding.inputRate);
            bufferLayout.attributeCount = 0;
            bufferLayout.attributes = nullptr;
            vertexBuffers.push_back(bufferLayout);
            attributesPerBinding.emplace_back();
        }

        for (const auto& attr : desc.vertexInputState.attributes)
        {
            if (attr.binding >= attributesPerBinding.size())
                continue;

            WGPUVertexAttribute attribute = {};
            attribute.format = convertVertexFormat(attr.format);
            attribute.offset = attr.offset;
            attribute.shaderLocation = attr.location;
            attributesPerBinding[attr.binding].push_back(attribute);
        }

        for (size_t i = 0; i < vertexBuffers.size(); ++i)
        {
            vertexBuffers[i].attributeCount = static_cast<uint32_t>(attributesPerBinding[i].size());
            vertexBuffers[i].attributes = attributesPerBinding[i].data();
        }

        WGPUVertexState vertexState = {};
        vertexState.nextInChain = nullptr;
        vertexState.module = vertexShader ? static_cast<const ShaderModule*>(vertexShader)->getHandle() : nullptr;
        vertexState.entryPoint = WGPUStringView{ "main", WGPU_STRLEN };
        vertexState.bufferCount = static_cast<uint32_t>(vertexBuffers.size());
        vertexState.buffers = vertexBuffers.data();

        WGPUPrimitiveState primitiveState = {};
        primitiveState.topology = convertPrimitiveTopology(desc.inputAssemblyState.topology);
        primitiveState.stripIndexFormat = WGPUIndexFormat_Undefined;
        primitiveState.frontFace = convertFrontFace(desc.rasterizationState.frontFace);
        primitiveState.cullMode = convertCullMode(desc.rasterizationState.cullMode);

        WGPUDepthStencilState depthStencilState = {};
        WGPUDepthStencilState* pDepthStencilState = nullptr;
        WGPUTextureFormat depthStencilFormat = convertTextureFormat(desc.depthStencilFormat);
        if (depthStencilFormat != WGPUTextureFormat_Undefined)
        {
            depthStencilState.format = depthStencilFormat;
            depthStencilState.depthWriteEnabled = desc.depthStencilState.depthWriteEnable ? WGPUOptionalBool_True : WGPUOptionalBool_False;
            depthStencilState.depthCompare = desc.depthStencilState.depthTestEnable ? convertCompareFunction(desc.depthStencilState.depthCompareOp) : WGPUCompareFunction_Always;
            depthStencilState.stencilFront.failOp = convertStencilOperation(desc.depthStencilState.front.failOp);
            depthStencilState.stencilFront.depthFailOp = convertStencilOperation(desc.depthStencilState.front.depthFailOp);
            depthStencilState.stencilFront.passOp = convertStencilOperation(desc.depthStencilState.front.passOp);
            depthStencilState.stencilFront.compare = convertCompareFunction(desc.depthStencilState.front.compareOp);
            depthStencilState.stencilBack.failOp = convertStencilOperation(desc.depthStencilState.back.failOp);
            depthStencilState.stencilBack.depthFailOp = convertStencilOperation(desc.depthStencilState.back.depthFailOp);
            depthStencilState.stencilBack.passOp = convertStencilOperation(desc.depthStencilState.back.passOp);
            depthStencilState.stencilBack.compare = convertCompareFunction(desc.depthStencilState.back.compareOp);
            depthStencilState.stencilReadMask = desc.depthStencilState.front.compareMask;
            depthStencilState.stencilWriteMask = desc.depthStencilState.front.writeMask;
            depthStencilState.depthBias = static_cast<int32_t>(desc.rasterizationState.depthBiasConstantFactor);
            depthStencilState.depthBiasSlopeScale = desc.rasterizationState.depthBiasSlopeFactor;
            depthStencilState.depthBiasClamp = desc.rasterizationState.depthBiasClamp;
            pDepthStencilState = &depthStencilState;
        }

        WGPUMultisampleState multisampleState = {};
        multisampleState.count = desc.multisampleState.sampleCount;
        multisampleState.mask = 0xFFFFFFFF;
        multisampleState.alphaToCoverageEnabled = desc.multisampleState.alphaToCoverageEnable;

        std::vector<WGPUColorTargetState> colorTargets;
        std::vector<WGPUBlendState> blendStates;
        colorTargets.reserve(desc.colorFormats.size());
        blendStates.reserve(desc.colorFormats.size());

        for (size_t i = 0; i < desc.colorFormats.size(); ++i)
        {
            const rhi::ColorBlendAttachmentState* src = (i < desc.colorBlendState.attachments.size()) ? &desc.colorBlendState.attachments[i] : nullptr;

            WGPUBlendState blendState = {};
            blendState.color.operation = convertBlendOperation(src ? src->colorBlendOp : rhi::BlendOp::Add);
            blendState.color.srcFactor = convertBlendFactor(src ? src->srcColorBlendFactor : rhi::BlendFactor::One);
            blendState.color.dstFactor = convertBlendFactor(src ? src->dstColorBlendFactor : rhi::BlendFactor::Zero);
            blendState.alpha.operation = convertBlendOperation(src ? src->alphaBlendOp : rhi::BlendOp::Add);
            blendState.alpha.srcFactor = convertBlendFactor(src ? src->srcAlphaBlendFactor : rhi::BlendFactor::One);
            blendState.alpha.dstFactor = convertBlendFactor(src ? src->dstAlphaBlendFactor : rhi::BlendFactor::Zero);
            blendStates.push_back(blendState);

            WGPUColorTargetState target = {};
            target.format = convertTextureFormat(desc.colorFormats[i]);
            target.blend = src && src->blendEnable ? &blendStates.back() : nullptr;
            target.writeMask = convertColorWriteMask(src ? src->colorWriteMask : 0xFF);
            colorTargets.push_back(target);
        }

        WGPUFragmentState fragmentState = {};
        fragmentState.nextInChain = nullptr;
        fragmentState.module = fragmentShader ? static_cast<const ShaderModule*>(fragmentShader)->getHandle() : nullptr;
        fragmentState.entryPoint = WGPUStringView{ "main", WGPU_STRLEN };
        fragmentState.targetCount = static_cast<uint32_t>(colorTargets.size());
        fragmentState.targets = colorTargets.data();

        WGPURenderPipelineDescriptor pipelineDesc = {};
        pipelineDesc.nextInChain = nullptr;
        pipelineDesc.label = WGPUStringView{ "UnnamedGraphicsPipeline", WGPU_STRLEN };
        pipelineDesc.layout = layout;
        pipelineDesc.vertex = vertexState;
        pipelineDesc.primitive = primitiveState;
        pipelineDesc.depthStencil = pDepthStencilState;
        pipelineDesc.multisample = multisampleState;
        pipelineDesc.fragment = &fragmentState;

        mRenderPipeline = wgpuDeviceCreateRenderPipeline(wgpuDevice, &pipelineDesc);
        if (!mRenderPipeline)
        {
            LOG_ERROR("Failed to create WebGPU graphics pipeline");
        }
    }

    Pipeline::Pipeline(const Device& device, const rhi::ComputePipelineDesc& desc)
        : rhi::Pipeline(device, *desc.pipelineLayout, rhi::PipelineType::Compute)
    {
        WGPUDevice wgpuDevice = device.getHandle();
        WGPUPipelineLayout layout = static_cast<const PipelineLayout*>(desc.pipelineLayout)->getHandle();

        WGPUComputePipelineDescriptor pipelineDesc = {};
        pipelineDesc.nextInChain = nullptr;
        pipelineDesc.label = WGPUStringView{ "UnnamedComputePipeline", WGPU_STRLEN };
        pipelineDesc.layout = layout;
        pipelineDesc.compute.module = desc.shaderModule ? static_cast<const ShaderModule*>(desc.shaderModule)->getHandle() : nullptr;
        pipelineDesc.compute.entryPoint = WGPUStringView{ "main", WGPU_STRLEN };

        mComputePipeline = wgpuDeviceCreateComputePipeline(wgpuDevice, &pipelineDesc);
        if (!mComputePipeline)
        {
            LOG_ERROR("Failed to create WebGPU compute pipeline");
        }
    }

    Pipeline::~Pipeline()
    {
        if (mRenderPipeline)
        {
            wgpuRenderPipelineRelease(mRenderPipeline);
            mRenderPipeline = nullptr;
        }
        if (mComputePipeline)
        {
            wgpuComputePipelineRelease(mComputePipeline);
            mComputePipeline = nullptr;
        }
    }
}
