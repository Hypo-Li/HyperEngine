#pragma once
#include "Material.h"
#include "MaterialFunctionLibrary.h"
#include "MaterialParameter.h"
#include "MaterialResourceObserver.h"

#include <Core/Common/Types.h>
#include <RenderBase/Shader.h>

namespace hyp
{
    class MaterialInstance;
    class MaterialTemplate : public Material
    {
        REFLECT_CLASS(MaterialTemplate)
        friend class MaterialFunctionLibrary;
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

        bool addParameter(HString name, const MaterialParameterValue& defaultValue, HString group = HString());

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

        // 只支持交换两个下标(编辑器的上移/下移按钮一次只跟相邻一项换位), 不提供"移动到任意
        // 下标"的通用接口——当前没有用得上后者的场景, 不提前设计。参数顺序决定了生成的
        // MaterialParameters cbuffer 布局, 交换后要重新编译, 所以跟 addParameter/removeParameter
        // 一样标 DirtyFlags::Pipeline。
        bool swapParameters(uint32_t indexA, uint32_t indexB) noexcept;

        std::span<const MaterialParameter> getParameters() const noexcept
        {
            return mParameters;
        }

        // 参数组——纯编辑器展示元数据(MaterialTemplateEditor/MaterialInstanceEditor 按组折叠
        // 展示参数用), 不影响 shader 生成, 不标 DirtyFlags::Pipeline。组列表本身立即生效,
        // 不像 mParameters 那样走"pending + Apply"的间接层。
        bool addParameterGroup(HString name) noexcept;

        void removeParameterGroup(HString name) noexcept;

        bool renameParameterGroup(HString oldName, HString newName) noexcept;

        // 只支持交换两个下标, 跟 swapParameters() 同一个理由(编辑器上移/下移按钮), 不提供
        // "移动到任意下标"的通用接口
        bool swapParameterGroups(uint32_t indexA, uint32_t indexB) noexcept;

        std::span<const HString> getParameterGroups() const noexcept
        {
            return mParameterGroups;
        }

        // 给已存在的参数改组; 参数不存在时返回 false, 不影响 DirtyFlags::Pipeline
        bool setParameterGroup(HString parameterName, HString group) noexcept;

        // 私有纹理——跟 mParameters 完全分开存放, 只能通过这几个纹理专用重载添加, 没有接受
        // 通用 MaterialParameterValue 的入口, 所以不存在"私有的非纹理参数"这种状态。
        // MaterialInstance 的 override 系统只查 mParameters, 找不到私有纹理的名字, 天然没有
        // 暴露给 Instance 的路径。
        bool addPrivateTexture(HString name, Texture2D* texture, std::optional<SampleState> sampleStateOverride = std::nullopt);

        bool addPrivateTexture(HString name, Texture2DArray* texture, std::optional<SampleState> sampleStateOverride = std::nullopt);

        bool addPrivateTexture(HString name, Texture3D* texture, std::optional<SampleState> sampleStateOverride = std::nullopt);

        bool addPrivateTexture(HString name, TextureCube* texture, std::optional<SampleState> sampleStateOverride = std::nullopt);

        // 类型必须跟已有值一致(还是同一种纹理类型), 用来改"指向哪张纹理/采样状态", 不能把
        // 一个私有纹理槽位换成别的类型。
        bool setPrivateTextureValue(uint32_t index, const MaterialParameterValue& newValue);

        bool setPrivateTextureValue(HString name, const MaterialParameterValue& newValue);

        void removePrivateTexture(uint32_t index);

        void removePrivateTexture(HString name);

        uint32_t getPrivateTextureIndex(HString name) const noexcept;

        std::span<const MaterialParameter> getPrivateTextures() const noexcept
        {
            return mPrivateTextures;
        }

        void setMaterialSource(std::string materialSource) noexcept;

        const std::string& getMaterialSource() const noexcept
        {
            return mMaterialSource;
        }

        // 新增/移除都标 DirtyFlags::Pipeline——最终拼进 shader 的内容变了, 需要重新编译
        bool addMaterialFunctionLibrary(MaterialFunctionLibrary* library);

        void removeMaterialFunctionLibrary(MaterialFunctionLibrary* library);

        // addMaterialFunctionLibrary(library) 现在调用会不会成功——供编辑器在资产选择框里
        // 预先排除掉选了就会失败的候选项用
        bool canAddMaterialFunctionLibrary(const MaterialFunctionLibrary* library) const noexcept;

        std::span<const TRef<MaterialFunctionLibrary>> getMaterialFunctionLibraries() const noexcept
        {
            return mMaterialFunctionLibraries;
        }

        void setVertexPositionOffsetSource(std::string vertexPositionOffsetSource) noexcept;

        const std::string& getVertexPositionOffsetSource() const noexcept
        {
            return mVertexPositionOffsetSource;
        }

        bool hasVertexPositionOffset() const noexcept
        {
            return !mVertexPositionOffsetSource.empty();
        }

        // 缓存值, 见 apply()/postLoad()
        bool isTimeDependent() const noexcept
        {
            return mIsTimeDependent;
        }

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

        void setAnisotropic(bool anisotropic) noexcept;

        bool isAnisotropic() const noexcept
        {
            return mAnisotropic;
        }

        void apply();

        void postLoad() override;

        DirtyFlags getDirtyFlags() const noexcept
        {
            return mDirtyFlags;
        }

        MaterialShaderHandle getOrCreateMaterialShader();

        void registerInstance(MaterialInstance* instance);

        void unregisterInstance(MaterialInstance* instance);

        void onParameterResourceChanged() override;

    private:
        void createMaterialProxy(MaterialProxyHandle handle) override;

        void unrefMaterialShader();

        void updateAllParameters();

        void updateMaterialProxy();

        std::vector<MaterialParameterMetadata> makeParameterMetadata() const;

        // mParameters + mPrivateTextures, in that order — same order makeParameterMetadata()
        // uses, which is what the shader's binding slot indices are derived from.
        std::vector<MaterialParameter> makeEffectiveParameterList() const;

        bool addPrivateTextureValue(HString name, MaterialParameterValue value);

        // 被 MaterialFunctionLibrary::notifyDependents() 级联调用——直接引用的库变了,
        // 或者嵌套引用到的某个库变了, 都会走到这里
        void onFunctionLibraryUpdated();

        void syncResourceObserver();

    private:
        MaterialDomain mMaterialDomain = MaterialDomain::Surface;
        BlendMode mBlendMode = BlendMode::Opaque;
        float mAlphaCutoff = 0.3333f;
        ShadingModel mShadingModel = ShadingModel::Standard;

        std::vector<MaterialParameter> mParameters;
        std::vector<MaterialParameter> mPrivateTextures;
        std::vector<HString> mParameterGroups;
        std::string mMaterialSource;
        std::string mVertexPositionOffsetSource;
        std::vector<TRef<MaterialFunctionLibrary>> mMaterialFunctionLibraries;
        bool mIsTimeDependent = false;

        bool mDoubleSided;
        bool mTangentSpaceNormal;
        bool mAnisotropic = false;
        DirtyFlags mDirtyFlags = DirtyFlags::None;

        MaterialShaderHandle mShaderHandle;
        bool mShaderHandleDirty = true;

        std::vector<MaterialInstance*> mInstances;

        MaterialResourceObserver mResourceObserver;
    };

    ENABLE_ENUM_CLASS_BITWISE(MaterialTemplate::DirtyFlags)

    template <> Type* Reflection::createType<MaterialTemplate>();
}
