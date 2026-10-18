#include "MaterialTemplate.h"
#include "MaterialInstance.h"

#include "../Context.h"
#include <RenderBase/RenderFrontend.h>
#include <RenderBase/MaterialProxy.h>

#include <algorithm>
#include <cctype>
#include <utility>

namespace hyp
{
    // 边界匹配, 避免命中 "wrappedTime" 这类子串
    static bool containsIdentifier(const std::string& source, std::string_view identifier) noexcept
    {
        auto isIdentifierChar = [](char c) noexcept
        {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        };

        size_t pos = 0;
        while ((pos = source.find(identifier, pos)) != std::string::npos)
        {
            bool boundaryBefore = pos == 0 || !isIdentifierChar(source[pos - 1]);
            size_t afterPos = pos + identifier.size();
            bool boundaryAfter = afterPos >= source.size() || !isIdentifierChar(source[afterPos]);
            if (boundaryBefore && boundaryAfter)
            {
                return true;
            }
            pos = afterPos;
        }
        return false;
    }

    REGISTER_CLASS(MaterialTemplate)

    MaterialTemplate::MaterialTemplate() :
        mDoubleSided(false),
        mTangentSpaceNormal(true),
        mResourceObserver(*this)
    {
    }

    MaterialTemplate::~MaterialTemplate()
    {
        unrefMaterialShader();

        for (const TRef<MaterialFunctionLibrary>& library : mMaterialFunctionLibraries)
            library.get()->unregisterDependent(this);
    }

    void MaterialTemplate::setMaterialDomain(MaterialDomain materialDomain) noexcept
    {
        mMaterialDomain = materialDomain;
        mDirtyFlags |= DirtyFlags::Pipeline;
        mShaderHandleDirty = true;
    }

    void MaterialTemplate::setBlendMode(BlendMode blendMode) noexcept
    {
        mBlendMode = blendMode;
        mDirtyFlags |= DirtyFlags::Pipeline;
    }

    void MaterialTemplate::setAlphaCutoff(float alphaCutoff) noexcept
    {
        mAlphaCutoff = alphaCutoff;
        mDirtyFlags |= DirtyFlags::Parameters;
    }

    void MaterialTemplate::setShadingModel(ShadingModel shadingModel) noexcept
    {
        mShadingModel = shadingModel;
        mDirtyFlags |= DirtyFlags::Pipeline;
    }

    bool MaterialTemplate::addParameter(HString name, const MaterialParameterValue& defaultValue, HString group)
    {
        if (getParameterIndex(name) < mParameters.size())
            return false;

        MaterialParameter& parameter = mParameters.emplace_back();
        parameter.name = name;
        parameter.value = defaultValue;
        parameter.group = group;

        mDirtyFlags |= DirtyFlags::Pipeline;
        mShaderHandleDirty = true;
        return true;
    }

    HString MaterialTemplate::getParameterName(uint32_t index) const noexcept
    {
        if (index < mParameters.size())
            return mParameters[index].name;
        return HString();
    }

    uint32_t MaterialTemplate::getParameterIndex(HString name) const noexcept
    {
        auto it = std::find_if(mParameters.begin(), mParameters.end(), [name](const MaterialParameter& parameter)
        {
            return parameter.name == name;
        });
        if (it != mParameters.end())
            return static_cast<uint32_t>(std::distance(mParameters.begin(), it));
        return static_cast<uint32_t>(mParameters.size());
    }

    bool MaterialTemplate::setParameterDefaultValue(uint32_t index, const MaterialParameterValue& newDefaultValue)
    {
        assert(index < mParameters.size());
        MaterialParameter& parameter = mParameters[index];
        if (parameter.value.getType() != newDefaultValue.getType())
            return false;
        parameter.value = newDefaultValue;

        mDirtyFlags |= DirtyFlags::Parameters;

        return true;
    }

    bool MaterialTemplate::setParameterDefaultValue(HString name, const MaterialParameterValue& newDefaultValue)
    {
        return setParameterDefaultValue(getParameterIndex(name), newDefaultValue);
    }

    std::optional<MaterialParameterValue> MaterialTemplate::getParameterDefaultValue(uint32_t index) const noexcept
    {
        if (index < mParameters.size())
            return mParameters[index].value;
        return std::nullopt;
    }

    std::optional<MaterialParameterValue> MaterialTemplate::getParameterDefaultValue(HString name) const noexcept
    {
        return getParameterDefaultValue(getParameterIndex(name));
    }

    void MaterialTemplate::removeParameter(uint32_t index)
    {
        if (index >= mParameters.size())
            return;

        mParameters.erase(mParameters.begin() + index);
        mDirtyFlags |= DirtyFlags::Pipeline;
        mShaderHandleDirty = true;
    }

    void MaterialTemplate::removeParameter(HString name)
    {
        removeParameter(getParameterIndex(name));
    }

    bool MaterialTemplate::swapParameters(uint32_t indexA, uint32_t indexB) noexcept
    {
        if (indexA >= mParameters.size() || indexB >= mParameters.size())
            return false;
        if (indexA == indexB)
            return true;

        std::swap(mParameters[indexA], mParameters[indexB]);
        mDirtyFlags |= DirtyFlags::Pipeline;
        mShaderHandleDirty = true;
        return true;
    }

    bool MaterialTemplate::addParameterGroup(HString name) noexcept
    {
        // "Common" 是空字符串(未分组参数)在编辑器里显示用的隐式组名, 不是一个真实存在于
        // mParameterGroups 里的条目——允许用户另建一个真的叫这个名字的组, 会跟那个隐式桶
        // 显示重名、混淆哪些参数究竟属于哪一个。
        if (name.empty() || name == HString("Common"))
            return false;
        auto it = std::find(mParameterGroups.begin(), mParameterGroups.end(), name);
        if (it != mParameterGroups.end())
            return false;

        mParameterGroups.push_back(name);
        return true;
    }

    void MaterialTemplate::removeParameterGroup(HString name) noexcept
    {
        std::erase(mParameterGroups, name);

        // 显式清空引用这个组的参数, 不能指望"字符串不匹配任何现存组就归 Common"这条隐式规则
        // 兜底——如果之后又新建一个同名组, 那些参数会被悄悄"收编"回去, 不是用户删组时预期的
        // 行为。
        for (MaterialParameter& parameter : mParameters)
        {
            if (parameter.group == name)
                parameter.group = HString();
        }
    }

    bool MaterialTemplate::renameParameterGroup(HString oldName, HString newName) noexcept
    {
        if (newName.empty() || newName == HString("Common"))
            return false;
        auto it = std::find(mParameterGroups.begin(), mParameterGroups.end(), oldName);
        if (it == mParameterGroups.end())
            return false;
        if (std::find(mParameterGroups.begin(), mParameterGroups.end(), newName) != mParameterGroups.end())
            return false;

        *it = newName;
        for (MaterialParameter& parameter : mParameters)
        {
            if (parameter.group == oldName)
                parameter.group = newName;
        }
        return true;
    }

    bool MaterialTemplate::swapParameterGroups(uint32_t indexA, uint32_t indexB) noexcept
    {
        if (indexA >= mParameterGroups.size() || indexB >= mParameterGroups.size())
            return false;
        if (indexA == indexB)
            return true;

        std::swap(mParameterGroups[indexA], mParameterGroups[indexB]);
        return true;
    }

    bool MaterialTemplate::setParameterGroup(HString parameterName, HString group) noexcept
    {
        uint32_t index = getParameterIndex(parameterName);
        if (index >= mParameters.size())
            return false;

        mParameters[index].group = group;
        return true;
    }

    bool MaterialTemplate::addPrivateTextureValue(HString name, MaterialParameterValue value)
    {
        if (getParameterIndex(name) < mParameters.size() || getPrivateTextureIndex(name) < mPrivateTextures.size())
            return false;

        MaterialParameter& texture = mPrivateTextures.emplace_back();
        texture.name = name;
        texture.value = value;

        mDirtyFlags |= DirtyFlags::Pipeline;
        mShaderHandleDirty = true;
        return true;
    }

    bool MaterialTemplate::addPrivateTexture(HString name, Texture2D* texture, std::optional<SampleState> sampleStateOverride)
    {
        return addPrivateTextureValue(name, sampleStateOverride ?
            MaterialParameterValue(texture, *sampleStateOverride) : MaterialParameterValue(texture));
    }

    bool MaterialTemplate::addPrivateTexture(HString name, Texture2DArray* texture, std::optional<SampleState> sampleStateOverride)
    {
        return addPrivateTextureValue(name, sampleStateOverride ?
            MaterialParameterValue(texture, *sampleStateOverride) : MaterialParameterValue(texture));
    }

    bool MaterialTemplate::addPrivateTexture(HString name, Texture3D* texture, std::optional<SampleState> sampleStateOverride)
    {
        return addPrivateTextureValue(name, sampleStateOverride ?
            MaterialParameterValue(texture, *sampleStateOverride) : MaterialParameterValue(texture));
    }

    bool MaterialTemplate::addPrivateTexture(HString name, TextureCube* texture, std::optional<SampleState> sampleStateOverride)
    {
        return addPrivateTextureValue(name, sampleStateOverride ?
            MaterialParameterValue(texture, *sampleStateOverride) : MaterialParameterValue(texture));
    }

    bool MaterialTemplate::setPrivateTextureValue(uint32_t index, const MaterialParameterValue& newValue)
    {
        if (index >= mPrivateTextures.size())
            return false;
        MaterialParameter& texture = mPrivateTextures[index];
        if (texture.value.getType() != newValue.getType())
            return false;
        texture.value = newValue;

        mDirtyFlags |= DirtyFlags::Parameters;
        return true;
    }

    bool MaterialTemplate::setPrivateTextureValue(HString name, const MaterialParameterValue& newValue)
    {
        return setPrivateTextureValue(getPrivateTextureIndex(name), newValue);
    }

    void MaterialTemplate::removePrivateTexture(uint32_t index)
    {
        if (index >= mPrivateTextures.size())
            return;

        mPrivateTextures.erase(mPrivateTextures.begin() + index);
        mDirtyFlags |= DirtyFlags::Pipeline;
        mShaderHandleDirty = true;
    }

    void MaterialTemplate::removePrivateTexture(HString name)
    {
        removePrivateTexture(getPrivateTextureIndex(name));
    }

    uint32_t MaterialTemplate::getPrivateTextureIndex(HString name) const noexcept
    {
        auto it = std::find_if(mPrivateTextures.begin(), mPrivateTextures.end(), [name](const MaterialParameter& texture)
        {
            return texture.name == name;
        });
        if (it != mPrivateTextures.end())
            return static_cast<uint32_t>(std::distance(mPrivateTextures.begin(), it));
        return static_cast<uint32_t>(mPrivateTextures.size());
    }

    void MaterialTemplate::setMaterialSource(std::string materialSource) noexcept
    {
        mMaterialSource = std::move(materialSource);
        mDirtyFlags |= DirtyFlags::Pipeline;
        mShaderHandleDirty = true;
    }

    bool MaterialTemplate::canAddMaterialFunctionLibrary(const MaterialFunctionLibrary* library) const noexcept
    {
        if (!library)
            return false;
        return std::find(mMaterialFunctionLibraries.begin(), mMaterialFunctionLibraries.end(), library) == mMaterialFunctionLibraries.end();
    }

    bool MaterialTemplate::addMaterialFunctionLibrary(MaterialFunctionLibrary* library)
    {
        if (!canAddMaterialFunctionLibrary(library))
            return false;

        mMaterialFunctionLibraries.emplace_back(library);
        library->registerDependent(this);
        mDirtyFlags |= DirtyFlags::Pipeline;
        mShaderHandleDirty = true;
        return true;
    }

    void MaterialTemplate::removeMaterialFunctionLibrary(MaterialFunctionLibrary* library)
    {
        auto it = std::find(mMaterialFunctionLibraries.begin(), mMaterialFunctionLibraries.end(), library);
        if (it == mMaterialFunctionLibraries.end())
            return;

        library->unregisterDependent(this);
        mMaterialFunctionLibraries.erase(it);
        mDirtyFlags |= DirtyFlags::Pipeline;
        mShaderHandleDirty = true;
    }

    void MaterialTemplate::onFunctionLibraryUpdated()
    {
        mDirtyFlags |= DirtyFlags::Pipeline;
        mShaderHandleDirty = true;
        apply();
    }

    void MaterialTemplate::setVertexPositionOffsetSource(std::string vertexPositionOffsetSource) noexcept
    {
        mVertexPositionOffsetSource = std::move(vertexPositionOffsetSource);
        mDirtyFlags |= DirtyFlags::Pipeline;
        mShaderHandleDirty = true;
    }

    void MaterialTemplate::setDoubleSided(bool doubleSided) noexcept
    {
        mDoubleSided = doubleSided;
        mDirtyFlags |= DirtyFlags::Pipeline;
    }

    void MaterialTemplate::setTangentSpaceNormal(bool tangentSpaceNormal) noexcept
    {
        mTangentSpaceNormal = tangentSpaceNormal;
        mDirtyFlags |= DirtyFlags::Pipeline;
    }

    void MaterialTemplate::setAnisotropic(bool anisotropic) noexcept
    {
        mAnisotropic = anisotropic;
        mDirtyFlags |= DirtyFlags::Pipeline;
    }

    void MaterialTemplate::apply()
    {
        if (mDirtyFlags == DirtyFlags::None)
            return;

        syncResourceObserver();

        if (enumHasAllFlags(mDirtyFlags, DirtyFlags::Pipeline))
        {
            mIsTimeDependent = containsIdentifier(mVertexPositionOffsetSource, "time");

            // 不能靠"模板自己有没有 MaterialProxy"来决定要不要让 mShaderHandle 失效——
            // MaterialInstance::updateMaterialProxy() 里的 mTemplate->getOrCreateMaterialShader()
            // 读的就是这份 handle, 它依赖的是"这个 handle 是不是还对应着编辑前的旧 shader",
            // 跟模板本身是否被直接绑到某个 Mesh 上(即模板自己是否有 MaterialProxy)完全无关——
            // 模板可能只是被若干 MaterialInstance 派生使用, 从未直接绑过。之前这一步被裹在
            // 下面的 proxyHandle.isValid() 判断里, 场景里没有直接用这个模板时 unrefMaterialShader()
            // 根本不会执行, mShaderHandle 就一直停在编辑前的旧值上, MaterialInstance 那边
            // getOrCreateMaterialShader() 看到 handle 仍然 valid, 直接原样返回旧 shader,
            // 材质源码改了也不会生效。
            unrefMaterialShader();

            // 模板自己的 MaterialProxy(如果有的话)的属性/shader 引用刷新, 只在真的存在时
            // 才需要做——不影响上面 unrefMaterialShader() 必须无条件执行这件事。
            auto proxyHandle = getMaterialProxyHandle();
            if (proxyHandle.isValid())
                updateMaterialProxy();
        }
        else if (enumHasAllFlags(mDirtyFlags, DirtyFlags::Parameters))
        {
            auto proxyHandle = getMaterialProxyHandle();
            if (proxyHandle.isValid())
                updateAllParameters();
        }

        for (MaterialInstance* instance : mInstances)
        {
            instance->onTemplateUpdated(mDirtyFlags);
        }

        mDirtyFlags = DirtyFlags::None;
    }

    void MaterialTemplate::postLoad()
    {
        mIsTimeDependent = containsIdentifier(mVertexPositionOffsetSource, "time");
        syncResourceObserver();
    }

    void MaterialTemplate::syncResourceObserver()
    {
        std::vector<MaterialParameterValue> values;
        values.reserve(mParameters.size() + mPrivateTextures.size());
        for (const MaterialParameter& parameter : mParameters)
            values.push_back(parameter.value);
        for (const MaterialParameter& texture : mPrivateTextures)
            values.push_back(texture.value);
        mResourceObserver.sync(values);
    }

    void MaterialTemplate::onParameterResourceChanged()
    {
        if (getMaterialProxyHandle().isValid())
            updateAllParameters();

        for (MaterialInstance* instance : mInstances)
            instance->onTemplateUpdated(DirtyFlags::Parameters);
    }

    MaterialShaderHandle MaterialTemplate::getOrCreateMaterialShader()
    {
        auto& renderFrontend = Context::get().getRenderFrontend();
        if (!mShaderHandle.isValid())
        {
            auto& materialShaderManager = renderFrontend.getMaterialShaderManager();
            mShaderHandle = materialShaderManager.allocateHandle();
            mShaderHandleDirty = false;
            renderFrontend.enqueueLambda(
                [shaderHandle = mShaderHandle,
                materialFunctionLibrarySource = resolveMaterialFunctionLibrarySources(mMaterialFunctionLibraries),
                materialSource = mMaterialSource,
                vertexPositionOffsetSource = mVertexPositionOffsetSource,
                parameterMetadata = makeParameterMetadata()
                ](FrameContext& context)
                {
                    auto& materialShaderManager = context.getMaterialShaderManager();
                    materialShaderManager.createMaterialShader(
                        shaderHandle,
                        "MaterialShader",
                        materialFunctionLibrarySource,
                        materialSource,
                        vertexPositionOffsetSource,
                        parameterMetadata
                    );
                }
            );
        }
        return mShaderHandle;
    }

    void MaterialTemplate::registerInstance(MaterialInstance* instance)
    {
        mInstances.push_back(instance);
        syncResourceObserver();
    }

    void MaterialTemplate::unregisterInstance(MaterialInstance* instance)
    {
        mInstances.erase(std::remove(mInstances.begin(), mInstances.end(), instance), mInstances.end());
    }

    void MaterialTemplate::createMaterialProxy(MaterialProxyHandle handle)
    {
        MaterialProxy::Attributes attributes;
        attributes.domain = mMaterialDomain;
        attributes.shadingModel = mShadingModel;
        attributes.blendMode = mBlendMode;
        attributes.alphaCutoff = mAlphaCutoff;
        attributes.doubleSided = mDoubleSided;
        attributes.tangentSpaceNormal = mTangentSpaceNormal;
        attributes.anisotropic = isAnisotropic();
        attributes.hasVertexPositionOffset = hasVertexPositionOffset();
        attributes.isTimeDependent = isTimeDependent();

        MaterialShaderHandle materialShaderHandle = getOrCreateMaterialShader();
        syncResourceObserver();

        std::vector<MaterialParameter> effectiveParameters = makeEffectiveParameterList();
        std::vector<MaterialProxy::Parameter> proxyParams;
        std::vector<std::pair<TextureProxyHandle, SamplerProxyHandle>> textureBindings;
        makeMaterialProxyParameters(effectiveParameters, proxyParams, textureBindings);

        Context::get().getRenderFrontend().enqueueLambda(
            [handle,
            attributes,
            materialShaderHandle,
            parameters = std::move(proxyParams),
            textureBindings = std::move(textureBindings)](FrameContext& context) mutable
            {
                auto& proxyManager = context.getProxyManager();
                resolveTextureBindings(parameters, textureBindings, proxyManager);

                auto& materialShaderManager = context.getMaterialShaderManager();
                MaterialShader* materialShader = materialShaderManager.getMaterialShader(materialShaderHandle);

                proxyManager.createProxy(
                    handle,
                    attributes,
                    materialShader,
                    parameters
                );
            }
        );

        mDirtyFlags = DirtyFlags::None;
    }

    void MaterialTemplate::unrefMaterialShader()
    {
        if (!mShaderHandle.isValid())
            return;

        // 只管把 MaterialTemplate 自己这一份引用还给渲染线程——这个 handle 的 index 什么时候
        // 能被下一次 allocateHandle() 复用, 完全由渲染线程那边的引用计数说了算(见
        // MaterialShaderManager::unrefMaterialShader() 的说明): 如果这个 shader 还有别的
        // 持有者(比如 MaterialInstance), 引用计数不会归零, index 也不会被交还, 不会出现
        // 逻辑线程这边立刻把 index 回收、渲染线程那边 slot 却还活着的窗口期。
        auto& renderFrontend = Context::get().getRenderFrontend();
        renderFrontend.enqueueLambda(
            [shaderHandle = mShaderHandle](FrameContext& context)
            {
                auto& materialShaderManager = context.getMaterialShaderManager();
                materialShaderManager.unrefMaterialShader(shaderHandle);
            }
        );

        // reset shader handle
        mShaderHandle = MaterialShaderHandle();
    }

    void MaterialTemplate::updateAllParameters()
    {
        MaterialProxyHandle proxyHandle = getMaterialProxyHandle();
        assert(proxyHandle.isValid());

        std::vector<MaterialParameter> effectiveParameters = makeEffectiveParameterList();
        std::vector<MaterialProxy::Parameter> proxyParams;
        std::vector<std::pair<TextureProxyHandle, SamplerProxyHandle>> textureBindings;
        makeMaterialProxyParameters(effectiveParameters, proxyParams, textureBindings);

        auto& renderFrontend = Context::get().getRenderFrontend();
        renderFrontend.enqueueLambda(
            [proxyHandle,
            parameters = std::move(proxyParams),
            textureBindings = std::move(textureBindings)
            ](FrameContext& context) mutable
            {
                auto& proxyManager = context.getProxyManager();
                resolveTextureBindings(parameters, textureBindings, proxyManager);
                MaterialProxy* proxy = proxyManager.getProxy(proxyHandle);
                proxy->updateParameters(parameters);
            }
        );
    }

    void MaterialTemplate::updateMaterialProxy()
    {
        MaterialProxy::Attributes attributes;
        attributes.domain = mMaterialDomain;
        attributes.shadingModel = mShadingModel;
        attributes.blendMode = mBlendMode;
        attributes.alphaCutoff = mAlphaCutoff;
        attributes.doubleSided = mDoubleSided;
        attributes.tangentSpaceNormal = mTangentSpaceNormal;
        attributes.anisotropic = isAnisotropic();
        attributes.hasVertexPositionOffset = hasVertexPositionOffset();
        attributes.isTimeDependent = isTimeDependent();

        MaterialShaderHandle materialShaderHandle = getOrCreateMaterialShader();

        std::vector<MaterialParameter> effectiveParameters = makeEffectiveParameterList();
        std::vector<MaterialProxy::Parameter> proxyParams;
        std::vector<std::pair<TextureProxyHandle, SamplerProxyHandle>> textureBindings;
        makeMaterialProxyParameters(effectiveParameters, proxyParams, textureBindings);

        Context::get().getRenderFrontend().enqueueLambda(
            [handle = getMaterialProxyHandle(),
            attributes,
            materialShaderHandle,
            parameters = std::move(proxyParams),
            textureBindings = std::move(textureBindings)](FrameContext& context) mutable
            {
                auto& proxyManager = context.getProxyManager();
                resolveTextureBindings(parameters, textureBindings, proxyManager);

                auto& materialShaderManager = context.getMaterialShaderManager();
                MaterialShader* materialShader = materialShaderManager.getMaterialShader(materialShaderHandle);
                MaterialProxy* materialProxy = proxyManager.getProxy(handle);
                materialProxy->update(
                    attributes,
                    materialShader,
                    parameters
                );
            }
        );
    }

    std::vector<MaterialParameterMetadata> MaterialTemplate::makeParameterMetadata() const
    {
        std::vector<MaterialParameterMetadata> metadata;
        metadata.reserve(mParameters.size() + mPrivateTextures.size());
        for (const auto& parameter : mParameters)
        {
            MaterialParameterMetadata meta;
            meta.name = parameter.name;
            meta.type = parameter.value.getType();
            if (meta.type == MaterialParameterType::Font)
            {
                const FontParameterNames names = makeFontParameterNames(parameter.name);

                meta.type = MaterialParameterType::Texture_2D_Array;
                metadata.push_back(meta);

                meta.name = names.sdfType;
                meta.type = MaterialParameterType::Int;
                metadata.push_back(meta);

                meta.name = names.spreadUv;
                meta.type = MaterialParameterType::Float;
            }
            metadata.push_back(meta);
        }
        // 私有纹理排在公开参数之后——这个顺序要跟 createMaterialProxy()/
        // MaterialInstance::makeEffectiveParameters() 里拼装的运行时绑定列表严格一致, shader
        // 里算出来的绑定槽位下标才对得上实际传的资源。
        for (const auto& texture : mPrivateTextures)
        {
            MaterialParameterMetadata meta;
            meta.name = texture.name;
            meta.type = texture.value.getType();
            metadata.push_back(meta);
        }
        return metadata;
    }

    std::vector<MaterialParameter> MaterialTemplate::makeEffectiveParameterList() const
    {
        std::vector<MaterialParameter> effective;
        effective.reserve(mParameters.size() + mPrivateTextures.size());
        effective.insert(effective.end(), mParameters.begin(), mParameters.end());
        effective.insert(effective.end(), mPrivateTextures.begin(), mPrivateTextures.end());
        return effective;
    }

    template <> Type* Reflection::createType<MaterialTemplate>()
    {
        auto c = new TClass<MaterialTemplate, Material>("MaterialTemplate");
        c->setDisplayName("Material Template");
        c->addProperty("Material Domain", &MaterialTemplate::mMaterialDomain);
        c->addProperty("Blend Mode", &MaterialTemplate::mBlendMode);
        c->addProperty("Shading Model", &MaterialTemplate::mShadingModel);
        c->addProperty("Alpha Cutoff", &MaterialTemplate::mAlphaCutoff);
        c->addProperty("Parameters", &MaterialTemplate::mParameters);
        c->addProperty("Private Textures", &MaterialTemplate::mPrivateTextures);
        c->addProperty("Parameter Groups", &MaterialTemplate::mParameterGroups);
        c->addProperty("Material Source", &MaterialTemplate::mMaterialSource);
        c->addProperty("Vertex Position Offset Source", &MaterialTemplate::mVertexPositionOffsetSource);
        c->addProperty("Material Function Libraries", &MaterialTemplate::mMaterialFunctionLibraries);
        c->addProperty("Double Sided", &MaterialTemplate::mDoubleSided);
        c->addProperty("Tangent Space Normal", &MaterialTemplate::mTangentSpaceNormal);
        c->addProperty("Anisotropic", &MaterialTemplate::mAnisotropic);
        return c;
    }
}
