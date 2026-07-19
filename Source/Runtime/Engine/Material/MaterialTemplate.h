#pragma once
#include "Material.h"
#include "MaterialParameter.h"

#include <Core/Common/Types.h>
#include <RenderBase/Shader.h>

namespace hyp
{
    class MaterialInstance;
    class MaterialTemplate : public Material
    {
        REFLECT_CLASS(MaterialTemplate)
    public:
        MaterialTemplate();

        ~MaterialTemplate();

        enum class DirtyFlags : uint8_t
        {
            None = 0,
            Parameters = 1 << 0,
            Pipeline = 1 << 1,
        };
        ENABLE_ENUM_CLASS_BITWISE_FRIEND(DirtyFlags);

        void setMaterialDomain(MaterialDomain materialDomain) noexcept;

        MaterialDomain getMaterialDomain() const noexcept
        {
            return mMaterialDomain;
        }

        void setBlendMode(BlendMode blendMode) noexcept;

        BlendMode getBlendMode() const noexcept
        {
            return mBlendMode;
        }

        void setAlphaCutoff(float alphaCutoff) noexcept;

        float getAlphaCutoff() const noexcept
        {
            return mAlphaCutoff;
        }

        void setShadingModel(ShadingModel shadingModel) noexcept;

        ShadingModel getShadingModel() const noexcept
        {
            return mShadingModel;
        }

        bool addParameter(HString name, const MaterialParameterValue& defaultValue);

        uint32_t getParameterCount() const noexcept
        {
            return static_cast<uint32_t>(mParameters.size());
        }

        HString getParameterName(uint32_t index) const noexcept;

        uint32_t getParameterIndex(HString name) const noexcept;

        bool setParameterDefaultValue(uint32_t index, const MaterialParameterValue& newDefaultValue);

        template <typename... Args>
        bool setParameterDefaultValue(uint32_t index, Args... args)
        {
            return setParameterDefaultValue(index, MaterialParameterValue(std::forward<Args>(args)...));
        }

        bool setParameterDefaultValue(HString name, const MaterialParameterValue& newDefaultValue);

        template <typename... Args>
        bool setParameterDefaultValue(HString name, Args... args)
        {
            return setParameterDefaultValue(name, MaterialParameterValue(std::forward<Args>(args)...));
        }

        std::optional<MaterialParameterValue> getParameterDefaultValue(uint32_t index) const noexcept;

        std::optional<MaterialParameterValue> getParameterDefaultValue(HString name) const noexcept;

        void removeParameter(uint32_t index);

        void removeParameter(HString name);

        std::span<const MaterialParameter> getParameters() const noexcept
        {
            return mParameters;
        }

        void setMaterialSource(std::string materialSource) noexcept;

        const std::string& getMaterialSource() const noexcept
        {
            return mMaterialSource;
        }

        void setVertexPositionOffsetSource(std::string vertexPositionOffsetSource) noexcept;

        const std::string& getVertexPositionOffsetSource() const noexcept
        {
            return mVertexPositionOffsetSource;
        }

        // 是否定义了顶点位置偏移(vertex position offset): 只看这份源码字符串是不是空,
        // 不做语法级别的检测——空字符串就代表材质作者没写 evaluateVertexPositionOffset(),
        // shader 组装阶段会用一个返回 float3(0,0,0) 的默认实现补上
        bool hasVertexPositionOffset() const noexcept
        {
            return !mVertexPositionOffsetSource.empty();
        }

        void addShaderMacro(HString name, HString value);

        void removeShaderMacro(HString name);

        void setDoubleSided(bool doubleSided) noexcept;

        bool isDoubleSided() const noexcept
        {
            return mDoubleSided;
        }

        void setTangentSpaceNormal(bool tangentSpaceNormal) noexcept;

        bool isTangentSpaceNormal() const noexcept
        {
            return mTangentSpaceNormal;
        }

        void apply();

        MaterialShaderHandle getOrCreateMaterialShader();

        void registerInstance(MaterialInstance* instance);

        void unregisterInstance(MaterialInstance* instance);

    private:
        void createMaterialProxy(MaterialProxyHandle handle) override;

        void unrefMaterialShader();

        void updateAllParameters();

        void updateMaterialProxy();

        std::vector<MaterialParameterMetadata> makeParameterMetadata() const;

    private:
        MaterialDomain mMaterialDomain = MaterialDomain::Surface;
        BlendMode mBlendMode = BlendMode::Opaque;
        float mAlphaCutoff = 0.3333f;
        ShadingModel mShadingModel = ShadingModel::Standard;

        std::vector<MaterialParameter> mParameters;
        std::string mMaterialSource;
        std::string mVertexPositionOffsetSource;

        bool mDoubleSided;
        bool mTangentSpaceNormal;
        DirtyFlags mDirtyFlags = DirtyFlags::None;

        MaterialShaderHandle mShaderHandle;
        bool mShaderHandleDirty = true;

        std::vector<MaterialInstance*> mInstances;
    };

    ENABLE_ENUM_CLASS_BITWISE(MaterialTemplate::DirtyFlags)

    template <> Type* Reflection::createType<MaterialTemplate>();
}
