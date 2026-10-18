#include "Shader.h"

#include <format>
#include <string_view>

#include <Core/Common/Hash.h>

namespace hyp
{
    template <> Type* Reflection::createType<ShaderMacro>()
    {
        auto s = new TStructure<ShaderMacro>("ShaderMacro");
        s->addProperty("Name", &ShaderMacro::name);
        s->addProperty("Value", &ShaderMacro::value);
        return s;
    }

    namespace
    {
        std::string_view getParameterTypeName(MaterialParameterType type)
        {
            switch (type)
            {
            // bool 不是 WGSL 的 host-shareable 类型, 不能出现在 uniform 地址空间的结构体里
            // (Dawn 编译期直接拒绝)——统一用 uint 表示布尔材质参数(0/1), SPIR-V/HLSL 也兼容
            case MaterialParameterType::Bool: return "uint";
            case MaterialParameterType::Int: return "int";
            case MaterialParameterType::Float: return "float";
            case MaterialParameterType::Float2: return "float2";
            case MaterialParameterType::Float3: return "float3";
            case MaterialParameterType::Float4: return "float4";
            default: return "unknown";
            }
        }

        std::string_view getTextureTypeName(MaterialParameterType type)
        {
            switch (type)
            {
            case MaterialParameterType::Texture_2D: return "MatTexture2D";
            case MaterialParameterType::Texture_2D_Array: return "MatTexture2DArray";
            case MaterialParameterType::Texture_3D: return "MatTexture3D";
            case MaterialParameterType::Texture_Cube: return "MatTextureCube";
            default: return "MatTexture2D";
            }
        }

        std::string_view getBindlessTextureArrayName(MaterialParameterType type)
        {
            switch (type)
            {
            case MaterialParameterType::Texture_2D: return "BindlessTexture2Ds";
            case MaterialParameterType::Texture_2D_Array: return "BindlessTexture2DArrays";
            case MaterialParameterType::Texture_3D: return "BindlessTexture3Ds";
            case MaterialParameterType::Texture_Cube: return "BindlessTextureCubes";
            default: return "BindlessTexture2Ds";
            }
        }

        std::string makeParameterDefines(std::span<const MaterialParameterMetadata> parameterMetadata, bool bindlessEnabled)
        {
            std::string parameterDefines;
            if (parameterMetadata.empty())
                return parameterDefines;
            parameterDefines.reserve(1024);

            std::string structBody;
            structBody.reserve(512);

            std::string globalResources;
            globalResources.reserve(512);

            std::string macroPart;
            macroPart.reserve(512);

            uint32_t textureIndex = 0;
            for (const auto& parameter : parameterMetadata)
            {
                auto type = parameter.type;
                auto name = parameter.name.toStringView();

                if (isTextureParameterType(type))
                {
                    if (bindlessEnabled)
                    {
                        std::format_to(std::back_inserter(structBody),
                            "    uint {}TextureIndex;\n    uint {}SamplerIndex;\n", name, name);
                        std::format_to(std::back_inserter(macroPart),
                            "#define {}Texture {}[uMaterialParameters.{}TextureIndex]\n",
                            name, getBindlessTextureArrayName(type), name
                        );
                        std::format_to(std::back_inserter(macroPart),
                            "#define {}Sampler BindlessSamplers[uMaterialParameters.{}SamplerIndex]\n",
                            name, name
                        );
                    }
                    else
                    {
                        uint32_t imageBinding = 1 + 2 * textureIndex;
                        uint32_t samplerBinding = imageBinding + 1;
                        std::format_to(std::back_inserter(globalResources),
                            "[[vk::binding({}, 2)]]\n{} {}Texture;\n",
                            imageBinding, getTextureTypeName(type), name
                        );
                        std::format_to(std::back_inserter(globalResources),
                            "[[vk::binding({}, 2)]]\nSamplerState {}Sampler;\n",
                            samplerBinding, name
                        );
                        std::format_to(std::back_inserter(macroPart),
                            "#define {} {}Texture\n",
                            name, name
                        );
                        std::format_to(std::back_inserter(macroPart),
                            "#define {}Sampler {}Sampler\n",
                            name, name
                        );
                    }
                    ++textureIndex;
                }
                else
                {
                    const auto typeName = getParameterTypeName(type);
                    std::format_to(std::back_inserter(structBody),
                        "    {} {};\n", typeName, name);
                    std::format_to(std::back_inserter(macroPart),
                        "#define {} uMaterialParameters.{}\n", name, name);
                }
            }

            parameterDefines += "struct MaterialParameters\n{\n";
            parameterDefines += structBody;
            parameterDefines += "};\n[[vk::binding(0, 2)]]\nConstantBuffer<MaterialParameters> uMaterialParameters;\n";
            parameterDefines += globalResources;
            parameterDefines += macroPart;

            return parameterDefines;
        }
    }

    MaterialShader::MaterialShader(
        rhi::Backend rhiBackend,
        const rhi::Device& device,
        RenderTagRegistry& renderTagRegistry,
        ShaderCache& shaderCache,
        const std::filesystem::path& shaderDir,
        HString name,
        std::string&& materialImplementation
    ) : mRHIBackend(rhiBackend),
        mDevice(device),
        mRenderTagRegistry(renderTagRegistry),
        mShaderCache(shaderCache),
        mShaderDir(shaderDir),
        mName(name),
        mMaterialImplementation(std::move(materialImplementation))
    {
    }

    std::optional<ShaderVariant> MaterialShader::getOrCreateVariant(
        HString renderTag,
        VertexLayout vertexLayout,
        std::span<const ShaderMacro> shaderMacros
    )
    {
        VariantKey key = { renderTag, vertexLayout, std::vector<ShaderMacro>(shaderMacros.begin(), shaderMacros.end()) };
        auto it = mVariants.find(key);
        if (it != mVariants.end())
        {
            return it->second;
        }
        if (mFailedVariants.contains(key))
        {
            return std::nullopt;
        }

        std::optional<ShaderVariant> variant = createVariant(renderTag, vertexLayout, shaderMacros);
        if (!variant)
        {
            mFailedVariants.insert(std::move(key));
            return std::nullopt;
        }
        return mVariants[std::move(key)] = *variant;
    }

    std::optional<ShaderVariant> MaterialShader::createVariant(
        HString renderTag,
        VertexLayout vertexLayout,
        std::span<const ShaderMacro> shaderMacros
    )
    {
        auto& shaderFileName = mRenderTagRegistry.getShaderFileName(renderTag);
        const rhi::ShaderCompileTarget& shaderCompileTarget = mDevice.getShaderCompileTarget();
        rhi::ShaderCodeFormat codeFormat = shaderCompileTarget.codeFormat;

        // Release 下 material shader 应该是优化且不带调试信息的;和 RenderBackend.cpp 里
        // enableValidation 用的是同一个 _DEBUG 判断
#ifdef _DEBUG
        constexpr bool withDebugInfo = true;
#else
        constexpr bool withDebugInfo = false;
#endif

        uint128_t cacheKey = mShaderCache.computeVariantKey(
            renderTag,
            vertexLayout,
            mMaterialImplementation,
            shaderMacros,
            withDebugInfo
        );

        std::optional<ShaderCache::CachedVariant> cached = mShaderCache.tryLoad(cacheKey);
        if (!cached)
        {
            std::string shaderSource = mRenderTagRegistry.makeShaderSource(renderTag, mMaterialImplementation, vertexLayout);
            cached = mShaderCache.compileAndCache(
                cacheKey,
                mName.toCString(),
                shaderSource,
                shaderFileName.c_str(),
                shaderMacros,
                withDebugInfo
            );
        }

        if (!cached)
        {
            return std::nullopt;
        }

        ShaderVariant result;
        result.vertexModule = mDevice.createShaderModule(rhi::ShaderStage::Vertex, std::move(cached->vertexCode), codeFormat);
        std::string vertexModuleName = std::string(mName.toStringView()) + "_" + std::string(renderTag.toStringView()) + "_Vertex";
        result.vertexModule->setDebugName(std::string_view(vertexModuleName));

        result.fragmentModule = mDevice.createShaderModule(rhi::ShaderStage::Fragment, std::move(cached->fragmentCode), codeFormat);
        std::string fragmentModuleName = std::string(mName.toStringView()) + "_" + std::string(renderTag.toStringView()) + "_Fragment";
        result.fragmentModule->setDebugName(std::string_view(fragmentModuleName));

        return result;
    }

    bool MaterialShader::VariantKey::operator==(const VariantKey& other) const noexcept
    {
        return renderTag == other.renderTag && vertexLayout == other.vertexLayout && shaderMacros == other.shaderMacros;
    }

    std::size_t MaterialShader::VariantKeyHash::operator()(const VariantKey& key) const noexcept
    {
        size_t seed = 0;
        Hash::combine(seed, key.renderTag);
        Hash::combine(seed, key.vertexLayout);
        Hash::combine(seed, key.shaderMacros);
        return seed;
    }

    MaterialShaderManager::MaterialShaderManager(rhi::Backend rhiBackend, const rhi::Device& device, RenderTagRegistry& renderTagRegistry, const std::filesystem::path& shaderDir) :
        mRHIBackend(rhiBackend),
        mDevice(device),
        mRenderTagRegistry(renderTagRegistry),
        mShaderCache(renderTagRegistry, shaderDir, device.getShaderCompileTarget()),
        mShaderDir(shaderDir),
        mBindlessEnabled(device.getFeatures().bindless)
    {
    }

    MaterialShaderHandle MaterialShaderManager::allocateHandle()
    {
        std::lock_guard<std::mutex> lock(mHandleMutex);

        uint32_t index;
        uint32_t version;

        if (!mReclaimedIndices.empty())
        {
            index = mReclaimedIndices.back();
            mReclaimedIndices.pop_back();
            version = mVersions[index];
        }
        else
        {
            index = static_cast<uint32_t>(mVersions.size());
            version = 1;
            mVersions.push_back(version);
        }
        return MaterialShaderHandle(index, version);
    }

    // 材质没有自己写顶点位置偏移(vertex position offset)函数时补一个永远返回 0 的默认实现——
    // MaterialTemplate::hasVertexPositionOffset() 只看源码字符串是不是空, 真正"没写就给默认值"
    // 这一步放在这里做, 和 makeParameterDefines 拼接参数结构体是同一类"按需补全材质源码"的操作
    static std::string resolveVertexPositionOffsetSource(const std::string& vertexPositionOffsetSource)
    {
        if (!vertexPositionOffsetSource.empty())
        {
            return vertexPositionOffsetSource;
        }
        return "float3 evaluateVertexPositionOffset(in VertexPositionOffsetInput vpoi) { return float3(0, 0, 0); }\n";
    }

    // 兜底材质的 evaluateMaterial(): 只设品红自发光, 其余字段留给 MaterialOutput::__init() 的
    // 中性默认值(白色 baseColor、roughness 0.5 等)——不追求正确光照/GBuffer 内容, 只要求这个
    // 函数在任意 render tag 的模板里都能编译过
    static constexpr const char* kErrorMaterialSource =
        "void evaluateMaterial(in MaterialInput mi, inout MaterialOutput mo)\n"
        "{\n"
        "    mo.emissive = float3(4.0, 0.0, 4.0);\n"
        "}\n";

    void MaterialShaderManager::createMaterialShader(
        MaterialShaderHandle handle,
        HString name,
        const std::string& materialFunctionLibrarySource,
        const std::string& materialSource,
        const std::string& vertexPositionOffsetSource,
        std::span<const MaterialParameterMetadata> parameterMetadata
    )
    {
        uint32_t index = handle.getIndex();
        if (index >= mSlots.size())
        {
            mSlots.resize(index + 1);
        }
        auto& slot = mSlots[index];
        assert(slot.shader == nullptr);
        slot.version = handle.getVersion();
        slot.refCount = 1;

        // materialFunctionLibrarySource 不能拼进 vertexPositionOffsetSource 参数本身——
        // resolveVertexPositionOffsetSource() 靠这个参数是否为空判断用户写没写自己的顶点
        // 偏移函数, 拼进去会让这个判断永远落在"非空"分支
        std::string materialImplementation = makeParameterDefines(parameterMetadata, mBindlessEnabled)
            + materialFunctionLibrarySource
            + resolveVertexPositionOffsetSource(vertexPositionOffsetSource)
            + materialSource;
        slot.shader = std::make_unique<MaterialShader>(
            mRHIBackend,
            mDevice,
            mRenderTagRegistry,
            mShaderCache,
            mShaderDir,
            name,
            std::move(materialImplementation)
        );
    }

    void MaterialShaderManager::refMaterialShader(MaterialShaderHandle handle)
    {
        uint32_t index = handle.getIndex();
        assert(index < mSlots.size());
        auto& slot = mSlots[index];
        assert(slot.shader != nullptr && slot.version == handle.getVersion());
        slot.refCount++;
    }

    void MaterialShaderManager::unrefMaterialShader(MaterialShaderHandle handle)
    {
        uint32_t index = handle.getIndex();
        assert(index < mSlots.size());
        auto& slot = mSlots[index];
        assert(slot.shader != nullptr && slot.version == handle.getVersion());

        if (--slot.refCount == 0)
        {
            slot.shader = nullptr;
            slot.version = 0;

            // 只有这里(引用计数真正归零的这一刻)才允许这个 index 被下一次 allocateHandle()
            // 复用——不能让逻辑线程凭 MaterialTemplate 自己那一份引用就以为"可以回收了",
            // MaterialInstance::createMaterialProxy() 之类的地方会额外 refMaterialShader()
            // 一次, 那种情况下 slot.shader 还活着, 提前把 index 交还出去会导致后续复用同一个
            // index 建新 shader 时撞上 assert(slot.shader == nullptr)。bump mVersions[index]
            // 顺便让所有还攥着旧 handle 的地方天然失效(理论上不该再有, 但versioning本来就是
            // 干这个用的)。
            std::lock_guard<std::mutex> lock(mHandleMutex);
            mVersions[index]++;
            mReclaimedIndices.push_back(index);
        }
    }

    MaterialShader* MaterialShaderManager::getMaterialShader(MaterialShaderHandle handle) const
    {
        uint32_t index = handle.getIndex();
        assert(index < mSlots.size());
        auto& slot = mSlots[index];
        assert(slot.shader != nullptr && slot.version == handle.getVersion());
        return slot.shader.get();
    }

    MaterialShader* MaterialShaderManager::getOrCreateSharedGeometryShader(HString renderTag)
    {
        auto it = mSharedGeometryShaders.find(renderTag);
        if (it != mSharedGeometryShaders.end())
        {
            return it->second.get();
        }

        std::string materialImplementation = resolveVertexPositionOffsetSource("");
        auto shader = std::make_unique<MaterialShader>(
            mRHIBackend,
            mDevice,
            mRenderTagRegistry,
            mShaderCache,
            mShaderDir,
            HString("SharedGeometryShader_" + std::string(renderTag.toStringView())),
            std::move(materialImplementation)
        );
        MaterialShader* result = shader.get();
        mSharedGeometryShaders.emplace(renderTag, std::move(shader));
        return result;
    }

    ShaderVariant MaterialShaderManager::getErrorMaterialShader(HString renderTag, VertexLayout vertexLayout)
    {
        if (!mErrorMaterialShader)
        {
            mErrorMaterialShader = std::make_unique<MaterialShader>(
                mRHIBackend,
                mDevice,
                mRenderTagRegistry,
                mShaderCache,
                mShaderDir,
                "ErrorMaterialShader"_hs,
                resolveVertexPositionOffsetSource("") + kErrorMaterialSource
            );
        }

        auto variant = mErrorMaterialShader->getOrCreateVariant(renderTag, vertexLayout, {});
        assert(variant && "cook 阶段必须保证兜底材质对所有 renderTag 都能编译成功, 见 getErrorMaterialShader() 声明处的注释");
        return *variant;
    }

    PassShader::PassShader(
        const rhi::Device& device,
        HString name,
        const PassShaderBytecode& bytecode
    )
    {
        auto createModule = [&](rhi::ShaderStage stage, std::span<const uint8_t> stageBytecode, const char* stageLabel) -> TRef<rhi::ShaderModule>
        {
            if (stageBytecode.empty())
                return nullptr;

            std::vector<uint8_t> code(stageBytecode.begin(), stageBytecode.end());

            TRef<rhi::ShaderModule> module = device.createShaderModule(stage, std::move(code), bytecode.codeFormat);
            std::string debugName = std::string(name.toStringView()) + "_" + stageLabel;
            module->setDebugName(std::string_view(debugName));
            return module;
        };

        mVariant.vertexModule = createModule(rhi::ShaderStage::Vertex, bytecode.vertex, "Vertex");
        mVariant.fragmentModule = createModule(rhi::ShaderStage::Fragment, bytecode.fragment, "Fragment");
        mVariant.computeModule = createModule(rhi::ShaderStage::Compute, bytecode.compute, "Compute");
    }

}
