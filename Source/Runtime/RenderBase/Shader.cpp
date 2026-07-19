#include "Shader.h"
#include "ShaderCompiler.h"

#include <format>

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
            case MaterialParameterType::Bool: return "bool";
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
            case MaterialParameterType::Texture_2D: return "Texture2D";
            case MaterialParameterType::Texture_2D_Array: return "Texture2DArray";
            case MaterialParameterType::Texture_3D: return "Texture3D";
            case MaterialParameterType::Texture_Cube: return "TextureCube";
            default: return "Texture2D";
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
        HString name,
        std::string&& materialImplementation
    ) : mRHIBackend(rhiBackend),
        mDevice(device),
        mRenderTagRegistry(renderTagRegistry),
        mShaderCache(shaderCache),
        mName(name),
        mMaterialImplementation(std::move(materialImplementation))
    {
    }

    // 网格实际有几个 texcoord 由 VertexLayout 决定,和材质属性(makeShaderMacros)无关——
    // 这里按位置从低到高数出最高被置位的 TexCoordN, 取其 +1 作为 count, 假定顶点数据里
    // TexCoord0..N 是连续存在的(引擎目前所有生成/导入路径都保证这一点)
    static uint32_t computeTexCoordCount(VertexLayout vertexLayout) noexcept
    {
        uint32_t count = 0;
        for (uint32_t i = 0; i < 8; ++i)
        {
            if (enumHasAnyFlags(vertexLayout.getAttributeMask(), VertexAttributeMask(uint32_t(VertexAttributeMask::TexCoord0) << i)))
                count = i + 1;
        }
        return count;
    }

    ShaderVariant MaterialShader::getOrCreateVariant(
        HString renderTag,
        VertexLayout vertexLayout,
        std::span<const ShaderMacro> shaderMacros
    )
    {
        std::vector<ShaderMacro> allMacros(shaderMacros.begin(), shaderMacros.end());
        allMacros.emplace_back("TEXCOORD_COUNT"_hs, HString(std::to_string(computeTexCoordCount(vertexLayout))));
        bool hasVertexColor = enumHasAnyFlags(vertexLayout.getAttributeMask(), VertexAttributeMask::Color);
        allMacros.emplace_back("HAS_VERTEX_COLOR"_hs, HString(hasVertexColor ? "1" : "0"));

        VariantKey key = { renderTag, vertexLayout, allMacros };
        auto it = mVariants.find(key);
        if (it != mVariants.end())
        {
            return it->second;
        }
        ShaderVariant variant = createVariant(renderTag, vertexLayout, allMacros);
        return mVariants[std::move(key)] = variant;
    }

    ShaderVariant MaterialShader::createVariant(
        HString renderTag,
        VertexLayout vertexLayout,
        std::span<const ShaderMacro> shaderMacros
    )
    {
        ShaderVariant result;
        auto& shaderFileName = mRenderTagRegistry.getShaderFileName(renderTag);
        ShaderCompiler::ShaderStage stages[] = {
            ShaderCompiler::ShaderStage::Vertex,
            ShaderCompiler::ShaderStage::Fragment
        };
        rhi::ShaderCodeFormat codeFormat = mRHIBackend == rhi::Backend::WebGPU ?
            rhi::ShaderCodeFormat::Wgsl : rhi::ShaderCodeFormat::Spirv;

        // Release 下 material shader 应该是优化且不带调试信息的;和 RenderBackend.cpp 里
        // enableValidation 用的是同一个 _DEBUG 判断
#ifdef _DEBUG
        constexpr bool withDebugInfo = true;
#else
        constexpr bool withDebugInfo = false;
#endif

        uint64_t cacheKey = mShaderCache.computeVariantKey(
            renderTag,
            vertexLayout,
            mMaterialImplementation,
            shaderMacros,
            codeFormat,
            withDebugInfo,
            [this, renderTag, vertexLayout]() { return makeShaderSource(renderTag, vertexLayout); }
        );

        std::vector<std::vector<uint8_t>> shaderCode;
        if (auto cached = mShaderCache.tryLoad(cacheKey))
        {
            shaderCode.push_back(std::move(cached->vertexCode));
            shaderCode.push_back(std::move(cached->fragmentCode));
        }
        else
        {
            std::string shaderSource = makeShaderSource(renderTag, vertexLayout);

            if (mRHIBackend == rhi::Backend::Vulkan)
            {
                shaderCode = ShaderCompiler::compileToSpirvs(
                    mName.toCString(),
                    shaderSource.c_str(),
                    stages,
                    shaderFileName.c_str(),
                    shaderMacros,
                    true,
                    withDebugInfo
                );
            }
            else if (mRHIBackend == rhi::Backend::WebGPU)
            {
                shaderCode = ShaderCompiler::compileToWgsls(
                    mName.toCString(),
                    shaderSource.c_str(),
                    stages,
                    shaderFileName.c_str(),
                    shaderMacros,
                    true,
                    withDebugInfo
                );
            }

            mShaderCache.save(cacheKey, shaderCode[0], shaderCode[1]);
        }

        result.vertexModule = mDevice.createShaderModule(rhi::ShaderStage::Vertex, std::move(shaderCode[0]), codeFormat);
        std::string vertexModuleName = std::string(mName.toStringView()) + "_" + std::string(renderTag.toStringView()) + "_Vertex";
        result.vertexModule->setDebugName(std::string_view(vertexModuleName));

        result.fragmentModule = mDevice.createShaderModule(rhi::ShaderStage::Fragment, std::move(shaderCode[1]), codeFormat);
        std::string fragmentModuleName = std::string(mName.toStringView()) + "_" + std::string(renderTag.toStringView()) + "_Fragment";
        result.fragmentModule->setDebugName(std::string_view(fragmentModuleName));

        return result;
    }

    std::string MaterialShader::makeShaderSource(HString renderTag, VertexLayout vertexLayout)
    {
        std::string shaderSource = mRenderTagRegistry.getShaderTemplate(renderTag);

        std::string_view materialImplementationTag = "[MATERIAL_IMPLEMENTATION]";
        auto materialImplementationPos = shaderSource.find(materialImplementationTag);
        if (materialImplementationPos != std::string::npos)
        {
            shaderSource.replace(materialImplementationPos, materialImplementationTag.length(), mMaterialImplementation);
        }

        std::string_view vertexAttributeGettersTag = "[VERTEX_ATTRIBUTE_GETTERS]";
        auto vertexAttributeGettersPos = shaderSource.find(vertexAttributeGettersTag);
        if (vertexAttributeGettersPos != std::string::npos)
        {
            shaderSource.replace(
                vertexAttributeGettersPos,
                vertexAttributeGettersTag.length(),
                vertexLayout.getVertexAttributeGetters()
            );
        }

        return shaderSource;
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

    MaterialShaderManager::MaterialShaderManager(rhi::Backend rhiBackend, const rhi::Device& device, RenderTagRegistry& renderTagRegistry) :
        mRHIBackend(rhiBackend),
        mDevice(device),
        mRenderTagRegistry(renderTagRegistry),
        mShaderCache(renderTagRegistry),
        mBindlessEnabled(device.getFeatures().bindless)
    {
    }

    MaterialShaderHandle MaterialShaderManager::allocateHandle()
    {
        uint32_t index;
        uint32_t version;

        if (!mFreeIndices.empty())
        {
            index = mFreeIndices.back();
            mFreeIndices.pop_back();
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

    void MaterialShaderManager::freeHandle(MaterialShaderHandle handle)
    {
        uint32_t index = handle.getIndex();
        uint32_t version = handle.getVersion();

        if (mVersions[index] == version)
        {
            mVersions[index]++;
            mFreeIndices.push_back(index);
        }
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

    void MaterialShaderManager::createMaterialShader(
        MaterialShaderHandle handle,
        HString name,
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

        // 顶点位置偏移函数拼在参数定义后面、材质着色函数前面——它同样可能要读材质参数(比如
        // 风力强度), 必须和参数结构体定义在同一个作用域, 三段最终一起 splice 进 [MATERIAL_IMPLEMENTATION]
        std::string materialImplementation = makeParameterDefines(parameterMetadata, mBindlessEnabled)
            + resolveVertexPositionOffsetSource(vertexPositionOffsetSource)
            + materialSource;
        slot.shader = std::make_unique<MaterialShader>(
            mRHIBackend,
            mDevice,
            mRenderTagRegistry,
            mShaderCache,
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
            HString("SharedGeometryShader_" + std::string(renderTag.toStringView())),
            std::move(materialImplementation)
        );
        MaterialShader* result = shader.get();
        mSharedGeometryShaders.emplace(renderTag, std::move(shader));
        return result;
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
