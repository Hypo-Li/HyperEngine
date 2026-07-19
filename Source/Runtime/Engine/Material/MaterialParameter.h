#pragma once
#include <optional>

#include "../Texture/Texture2D.h"
#include "../Texture/Texture2DArray.h"
#include "../Texture/Texture3D.h"
#include "../Texture/TextureCube.h"

#include <Core/Math/Vector.h>
#include <RenderBase/MaterialTypes.h>
#include <RenderBase/MaterialProxy.h>
#include <RenderBase/ProxyManager.h>

namespace hyp
{
    template <typename T>
    struct material_parameter_type;

    template <>
    struct material_parameter_type<bool> { static constexpr MaterialParameterType value = MaterialParameterType::Bool; };

    template <>
    struct material_parameter_type<int> { static constexpr MaterialParameterType value = MaterialParameterType::Int; };

    template <>
    struct material_parameter_type<float> { static constexpr MaterialParameterType value = MaterialParameterType::Float; };

    template <>
    struct material_parameter_type<float2> { static constexpr MaterialParameterType value = MaterialParameterType::Float2; };

    template <>
    struct material_parameter_type<float3> { static constexpr MaterialParameterType value = MaterialParameterType::Float3; };

    template <>
    struct material_parameter_type<float4> { static constexpr MaterialParameterType value = MaterialParameterType::Float4; };

    struct SampledTexture2D
    {
        TRef<Texture2D> texture;
        std::optional<SampleState> sampleStateOverride;
    };
    template <> Type* Reflection::createType<SampledTexture2D>();

    struct SampledTexture2DArray
    {
        TRef<Texture2DArray> texture;
        std::optional<SampleState> sampleStateOverride;
    };
    template <> Type* Reflection::createType<SampledTexture2DArray>();

    struct SampledTexture3D
    {
        TRef<Texture3D> texture;
        std::optional<SampleState> sampleStateOverride;
    };
    template <> Type* Reflection::createType<SampledTexture3D>();

    struct SampledTextureCube
    {
        TRef<TextureCube> texture;
        std::optional<SampleState> sampleStateOverride;
    };
    template <> Type* Reflection::createType<SampledTextureCube>();

    template <typename T>
    struct is_sampled_texture : std::false_type {};

    template <>
    struct is_sampled_texture<SampledTexture2D> : std::true_type {};

    template <>
    struct is_sampled_texture<SampledTexture2DArray> : std::true_type {};

    template <>
    struct is_sampled_texture<SampledTexture3D> : std::true_type {};

    template <>
    struct is_sampled_texture<SampledTextureCube> : std::true_type {};

    template <typename T>
    inline constexpr bool is_sampled_texture_v = is_sampled_texture<T>::value;

    template <>
    struct material_parameter_type<SampledTexture2D> { static constexpr MaterialParameterType value = MaterialParameterType::Texture_2D; };

    template <>
    struct material_parameter_type<SampledTexture2DArray> { static constexpr MaterialParameterType value = MaterialParameterType::Texture_2D_Array; };

    template <>
    struct material_parameter_type<SampledTexture3D> { static constexpr MaterialParameterType value = MaterialParameterType::Texture_3D; };

    template <>
    struct material_parameter_type<SampledTextureCube> { static constexpr MaterialParameterType value = MaterialParameterType::Texture_Cube; };

    class MaterialParameterValue
    {
        // 反射需要直接访问 mValue 才能把底层 variant 注册成属性,复用现成的 std::variant
        // 反射机制,而不是为 MaterialParameterValue 单独手写一个 Special 序列化器
        friend class Reflection;
    public:
        MaterialParameterValue() : mValue(0.0f) {}

        MaterialParameterValue(bool value) : mValue(value) {}

        MaterialParameterValue(int value) : mValue(value) {}

        MaterialParameterValue(float value) : mValue(value) {}

        MaterialParameterValue(float2 value) : mValue(value) {}

        MaterialParameterValue(float3 value) : mValue(value) {}

        MaterialParameterValue(float4 value) : mValue(value) {}

        MaterialParameterValue(Texture2D* texture) : mValue(SampledTexture2D{ texture, std::nullopt }) {}

        MaterialParameterValue(Texture2D* texture, SampleState sampleState) : mValue(SampledTexture2D{ texture, sampleState }) {}

        MaterialParameterValue(Texture2DArray* texture) : mValue(SampledTexture2DArray{ texture, std::nullopt }) {}

        MaterialParameterValue(Texture2DArray* texture, SampleState sampleState) : mValue(SampledTexture2DArray{ texture, sampleState }) {}

        MaterialParameterValue(Texture3D* texture) : mValue(SampledTexture3D{ texture, std::nullopt }) {}

        MaterialParameterValue(Texture3D* texture, SampleState sampleState) : mValue(SampledTexture3D{ texture, sampleState }) {}

        MaterialParameterValue(TextureCube* texture) : mValue(SampledTextureCube{ texture, std::nullopt }) {}

        MaterialParameterValue(TextureCube* texture, SampleState sampleState) : mValue(SampledTextureCube{ texture, sampleState }) {}

        MaterialParameterType getType() const noexcept
        {
            return static_cast<MaterialParameterType>(mValue.index());
        }

        template <typename T>
        void set(T value)
        {
            assert(material_parameter_type<T>::value == getType());
            mValue = value;
        }

        template <typename T>
        T& get()
        {
            assert(material_parameter_type<T>::value == getType());
            return std::get<T>(mValue);
        }

        template <typename T>
        const T& get() const
        {
            assert(material_parameter_type<T>::value == getType());
            return std::get<T>(mValue);
        }

        template <typename LAMBDA>
        void visit(LAMBDA lambda)
        {
            std::visit(std::move(lambda), mValue);
        }

    private:
        using Value = std::variant<
            bool,
            int,
            float,
            float2,
            float3,
            float4,
            SampledTexture2D,
            SampledTexture2DArray,
            SampledTexture3D,
            SampledTextureCube
        >;
        Value mValue;
    };
    template <> Type* Reflection::createType<MaterialParameterValue>();

    struct MaterialParameter
    {
        HString name;
        MaterialParameterValue value;
    };
    template <> Type* Reflection::createType<MaterialParameter>();

    void makeMaterialProxyParameters(
        std::span<MaterialParameter> parameters,
        std::vector<MaterialProxy::Parameter>& outProxyParameters,
        std::vector<std::pair<TextureProxyHandle, SamplerProxyHandle>>& outTextureBindings
    );

    void resolveTextureBindings(
        std::span<MaterialProxy::Parameter> parameters,
        std::span<const std::pair<TextureProxyHandle, SamplerProxyHandle>> textureBindings,
        ProxyManager& proxyManager
    );

}
