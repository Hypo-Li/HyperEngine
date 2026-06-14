#pragma once
#include <variant>

#include "RenderProxy.h"
#include "Shader.h"
#include "Vulkan/Buffer.h"
#include "Vulkan/DescriptorSet.h"
#include "Vulkan/CommandBuffer.h"
#include "TextureProxy.h"
#include "SamplerProxy.h"
#include "MaterialTypes.h"
#include "RenderBaseDefines.h"

#include <Core/Common/HString.h>

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
            bool doubleSided = true;
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
            std::vector<ShaderMacro> shaderMacros,
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
            std::vector<ShaderMacro> shaderMacros,
            const std::vector<Parameter>& parameters
        );

        void updateParameters(std::span<const Parameter> updatedParameters);

        const vk::DescriptorSet* getParametersDescriptorSet() const noexcept
        {
            return mParametersDescriptorSet;
        }

        void prepare(vk::CommandBuffer& cmd);

        bool isParametersDirty() const noexcept
        {
            return mParametersDirty;
        }

        ShaderVariant getOrCreateShaderVariant(
            HString renderTag,
            VertexLayout vertexLayout
        ) noexcept;

        void cachePipeline(
            HString renderTag,
            VertexLayout vertexLayout,
            vk::Pipeline* pipeline
        );

        vk::Pipeline* getCachedPipeline(
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
        };

        void copyParameterBindingToUniformBufferCache(const ParameterBinding& binding);

        void resolveParameterBindings(const std::vector<Parameter>& parameters);

        void uploadToGPU(vk::CommandBuffer& cmd);

        uint32_t evaluateBuiltinRenderMask(Attributes attributes) noexcept;

        void createParameterResources();

        void destroyParameterResources();

    private:
        // id 有效位: 20 位
        uint32_t mId;
        uint32_t mRenderMask;
        Attributes mAttributes;
        MaterialShader* mMaterialShader;
        std::vector<ShaderMacro> mShaderMacros;

        std::vector<ParameterBinding> mParameterBindings;
        std::unordered_map<HString, uint32_t> mNameToBindingIndex;
        std::vector<uint8_t> mParametersBufferCache;

        TRef<vk::Buffer> mParametersBuffer;
        TRef<vk::DescriptorSet> mParametersDescriptorSet;
        bool mParametersDirty;

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
        std::unordered_map<PipelineLookupKey, TRef<vk::Pipeline>, PipelineLookupKeyHasher> mCachedPipelines;

    };

    template <>
    struct TProxyTraits<ProxyType::Material>
    {
        using Proxy = MaterialProxy;
    };
}
