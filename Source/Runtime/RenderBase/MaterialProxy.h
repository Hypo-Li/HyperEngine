#pragma once
#include <variant>

#include "RenderProxy.h"
#include "Shader.h"
#include "TextureProxy.h"
#include "SamplerProxy.h"
#include "MaterialTypes.h"

#include <Core/Common/HString.h>
#include <RHI/Buffer.h>
#include <RHI/BindGroup.h>
#include <RHI/CommandList.h>

namespace hyp
{
    class MaterialProxy : public RenderProxy
    {
    public:
        struct Attributes
        {
            MaterialDomain domain = MaterialDomain::Surface;
            ShadingModel shadingModel = ShadingModel::Standard;
            BlendMode blendMode = BlendMode::Opaque;
            float alphaCutoff = 0.3333f;
            bool doubleSided = true;
            bool tangentSpaceNormal = true;
            // 对应 MaterialTemplate::hasVertexPositionOffset(),渲染线程判断这个材质在
            // RenderTagFlags::Geometry_Only 的 render tag 下能否走跨材质共享的 shader/pipeline 时要用
            bool hasVertexPositionOffset = false;
        };

        struct Parameter
        {
            MaterialParameterType type;
            HString name;
            using ValueType = std::variant<
                bool,
                int,
                float,
                float2,
                float3,
                float4,
                std::pair<TextureProxy*, SamplerProxy*>
            >;
            ValueType value;
        };

        MaterialProxy(
            Attributes attributes,
            MaterialShader* materialShader,
            const std::vector<Parameter>& parameters
        );

        ~MaterialProxy() noexcept;

        uint32_t getId() const noexcept
        {
            return mId;
        }

        uint32_t getRenderMask() const noexcept
        {
            return mRenderMask;
        }

        Attributes getAttributes() const noexcept
        {
            return mAttributes;
        }

        MaterialShader* getMaterialShader() const noexcept
        {
            return mMaterialShader;
        }

        void update(
            Attributes attributes,
            MaterialShader* materialShader,
            const std::vector<Parameter>& parameters
        );

        void updateParameters(std::span<const Parameter> updatedParameters);

        const rhi::BindGroup* getParametersBindGroup() const noexcept
        {
            return mParametersBindGroup;
        }

        void prepare(rhi::CommandList& cmdList);

        bool isParametersDirty() const noexcept
        {
            return mParametersDirty || mTextureParametersDirty;
        }

        ShaderVariant getOrCreateShaderVariant(
            HString renderTag,
            VertexLayout vertexLayout
        ) noexcept;

        // RenderTagFlags::Geometry_Only 的 render tag 下,静态不透明且无顶点位置偏移的材质会被
        // 重定向到跨材质共享的 shader,此时不应该再绑材质参数 bind group(共享 shader 根本没有
        // 材质参数),getOrCreateShaderVariant 内部据此选择共享/独立 shader,这里暴露判断结果供
        // pass 侧决定要不要把材质参数 bind group layout 塞进 pipeline layout
        bool usesSharedGeometryShader(HString renderTag) const noexcept;

        void cachePipeline(
            HString renderTag,
            VertexLayout vertexLayout,
            rhi::Pipeline* pipeline
        );

        rhi::Pipeline* getCachedPipeline(
            HString renderTag,
            VertexLayout vertexLayout
        ) const noexcept;

    private:
        struct ParameterBinding
        {
            Parameter parameter;
            uint32_t offset;
            uint32_t size;
            uint32_t bindingSlot;

            ParameterBinding() = default;

            ParameterBinding(Parameter p, uint32_t o, uint32_t s, uint32_t slot)
                : parameter(p), offset(o), size(s), bindingSlot(slot)
            {
            }
        };

        void copyParameterBindingToUniformBufferCache(const ParameterBinding& binding);

        void buildParameterBindings(const std::vector<Parameter>& parameters);

        void uploadToGPU(rhi::CommandList& cmdList);

        uint32_t evaluateBuiltinRenderMask(Attributes attributes) noexcept;

        std::vector<ShaderMacro> makeShaderMacros(Attributes attributes) const;

        void createParameterResources();

        void destroyParameterResources();

    private:
        // id 有效位: 20 位
        uint32_t mId;
        uint32_t mRenderMask;
        Attributes mAttributes;
        MaterialShader* mMaterialShader;
        // 由 makeShaderMacros(mAttributes) 派生,传给 getOrCreateShaderVariant;同一个
        // mMaterialShader 可能被多个 Attributes 组合不同的 Proxy 共用,各自缓存各自的 variant
        std::vector<ShaderMacro> mShaderMacros;

        bool mBindlessEnabled;
        std::vector<ParameterBinding> mParameterBindings;
        std::unordered_map<HString, uint32_t> mNameToBindingIndex;
        std::vector<uint8_t> mParametersBufferCache;

        TRef<rhi::Buffer> mParametersBuffer;
        TRef<rhi::BindGroupLayout> mParametersBindGroupLayout;
        TRef<rhi::BindGroup> mParametersBindGroup;
        bool mParametersDirty = false;
        bool mTextureParametersDirty = false;

        struct PipelineLookupKey
        {
            VertexLayout vertexLayout;
            HString renderTag;

            bool operator==(const PipelineLookupKey& other) const
            {
                return vertexLayout == other.vertexLayout &&
                    renderTag == other.renderTag;
            }
        };

        struct PipelineLookupKeyHasher
        {
            size_t operator()(const PipelineLookupKey& key) const
            {
                size_t seed = 0;
                Hash::combine(seed, key.vertexLayout.getId());
                Hash::combine(seed, key.renderTag);
                return seed;
            }
        };
        std::unordered_map<PipelineLookupKey, TRef<rhi::Pipeline>, PipelineLookupKeyHasher> mCachedPipelines;

    };

    template <>
    struct TProxyTraits<ProxyType::Material>
    {
        using Proxy = MaterialProxy;
    };
}
