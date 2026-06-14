#include "Shader.h"
#include "ShaderCompiler.h"

#include <fstream>

#include <Core/Common/Hash.h>

namespace hyp
{
    MaterialShader::MaterialShader(
        const vk::Device& device,
        RenderTagRegistry& renderTagRegistry,
        HString name,
        std::string materialSource
    ) : mDevice(device),
        mRenderTagRegistry(renderTagRegistry),
        mName(name),
        mMaterialSource(std::move(materialSource))
    {
    }

    ShaderVariant MaterialShader::getOrCreateVariant(
        HString renderTag,
        VertexLayout vertexLayout,
        std::span<const ShaderMacro> shaderMacros
    )
    {
        // FIXME: 我们需要把ShaderMacros保存在Key中吗? 似乎没有必要,
        // 因为在同一个MaterialShader中, 宏应该都是一致的
        VariantKey key = { renderTag, vertexLayout };
        auto it = mVariants.find(key);
        if (it != mVariants.end())
        {
            return it->second;
        }
        return mVariants[key] = createVariant(renderTag, vertexLayout, shaderMacros);
    }

    ShaderVariant MaterialShader::createVariant(
        HString renderTag,
        VertexLayout vertexLayout,
        std::span<const ShaderMacro> shaderMacros
    )
    {
        ShaderVariant result;
        auto& shaderFileName = mRenderTagRegistry.getShaderFileName(renderTag);
        std::string shaderSource = makeShaderSource(renderTag, vertexLayout);
        ShaderCompiler::ShaderStage stages[] = {
            ShaderCompiler::ShaderStage::Vertex,
            ShaderCompiler::ShaderStage::Fragment
        };
        std::vector<std::vector<uint32_t>> spirvs = ShaderCompiler::compileToSpirvs(
            mName.toCString(),
            shaderSource.c_str(),
            stages,
            shaderFileName.c_str(),
            shaderMacros,
            true,
            true
        );
        std::vector<uint32_t> vertexSpirv = std::move(spirvs[0]);
        std::vector<uint32_t> fragmentSpirv = std::move(spirvs[1]);

        result.vertexModule = new vk::ShaderModule(mDevice, vertexSpirv);
        if (vkSetDebugUtilsObjectNameEXT)
        {
            VkDebugUtilsObjectNameInfoEXT nameInfo = {};
            nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
            nameInfo.objectType = VK_OBJECT_TYPE_SHADER_MODULE;
            nameInfo.objectHandle = uint64_t(result.vertexModule->getHandle());
            std::string name = std::string(mName.toStringView()) + "_" + std::string(renderTag.toStringView()) + "_Vertex";
            nameInfo.pObjectName = name.c_str();
            vkSetDebugUtilsObjectNameEXT(mDevice.getHandle(), &nameInfo);
        }

        result.fragmentModule = new vk::ShaderModule(mDevice, fragmentSpirv);
        if (vkSetDebugUtilsObjectNameEXT)
        {
            VkDebugUtilsObjectNameInfoEXT nameInfo = {};
            nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
            nameInfo.objectType = VK_OBJECT_TYPE_SHADER_MODULE;
            nameInfo.objectHandle = uint64_t(result.fragmentModule->getHandle());
            std::string name = std::string(mName.toStringView()) + "_" + std::string(renderTag.toStringView()) + "_Fragment";
            nameInfo.pObjectName = name.c_str();
            vkSetDebugUtilsObjectNameEXT(mDevice.getHandle(), &nameInfo);
        }

        return result;
    }

    std::string MaterialShader::makeShaderSource(HString renderTag, VertexLayout vertexLayout)
    {
        std::string shaderSource = mRenderTagRegistry.getShaderTemplate(renderTag);
        
        std::string_view materialSourceTag = "[MATERIAL_SOURCE]";
        auto materialSourcePos = shaderSource.find(materialSourceTag);
        if (materialSourcePos != std::string::npos)
        {
            shaderSource.replace(materialSourcePos, materialSourceTag.length(), mMaterialSource);
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
        return renderTag == other.renderTag && vertexLayout == other.vertexLayout;
    }

    std::size_t MaterialShader::VariantKeyHash::operator()(const VariantKey& key) const noexcept
    {
        size_t seed = 0;
        Hash::combine(seed, key.renderTag);
        Hash::combine(seed, key.vertexLayout);
        return seed;
    }

    MaterialShaderManager::MaterialShaderManager(const vk::Device& device, RenderTagRegistry& renderTagRegistry) :
        mDevice(device),
        mRenderTagRegistry(renderTagRegistry)
    {
    }

    MaterialShaderHandle MaterialShaderManager::allocateHandle()
    {
        uint32_t index;
        uint32_t version;

        if (!mFreeIndices.empty())
        {
            // 1. 从空闲池回收索引
            index = mFreeIndices.back();
            mFreeIndices.pop_back();
            version = mVersions[index];
        }
        else
        {
            // 2. 分配全新索引
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
            // 1. 递增版本号, 使所有持有该版本句柄的对象立即失效
            mVersions[index]++;
            mFreeIndices.push_back(index);
        }
    }

    void MaterialShaderManager::createMaterialShader(MaterialShaderHandle handle, HString name, const std::string& materialSource)
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
        slot.shader = std::make_unique<MaterialShader>(mDevice, mRenderTagRegistry, name, materialSource);
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

    PassShader::PassShader(const vk::Device& device, HString name, const std::filesystem::path& shaderPath) :
        mDevice(device),
        mName(name),
        mShaderFileName(shaderPath.string())
    {
        std::ifstream file(shaderPath);
        mShaderSource = std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());

        if (mShaderSource.find("VertexMain") != std::string::npos)
            mModuleFlags |= ModuleFlags::Vertex;
        if (mShaderSource.find("FragmentMain") != std::string::npos)
            mModuleFlags |= ModuleFlags::Fragment;
        if (mShaderSource.find("ComputeMain") != std::string::npos)
            mModuleFlags |= ModuleFlags::Compute;
    }

    ShaderVariant PassShader::getOrCreateVariant(std::span<const ShaderMacro> shaderMacros)
    {
        std::vector<ShaderMacro> key(shaderMacros.begin(), shaderMacros.end());
        auto it = mVariants.find(key);
        if (it != mVariants.end())
        {
            return it->second;
        }

        ShaderVariant variant = createVariant(shaderMacros);
        mVariants.emplace(std::move(key), variant);
        return variant;
    }

    ShaderVariant PassShader::createVariant(std::span<const ShaderMacro> shaderMacros)
    {
        ShaderVariant result;

        std::vector<ShaderCompiler::ShaderStage> stages;
        stages.reserve(3);
        if (enumHasAnyFlags(mModuleFlags, ModuleFlags::Vertex))
        {
            stages.push_back(ShaderCompiler::ShaderStage::Vertex);
        }
        if (enumHasAnyFlags(mModuleFlags, ModuleFlags::Fragment))
        {
            stages.push_back(ShaderCompiler::ShaderStage::Fragment);
        }
        if (enumHasAnyFlags(mModuleFlags, ModuleFlags::Compute))
        {
            stages.push_back(ShaderCompiler::ShaderStage::Compute);
        }

        std::vector<std::vector<uint32_t>> spirvs = ShaderCompiler::compileToSpirvs(
            mName.toCString(),
            mShaderSource.c_str(),
            stages,
            mShaderFileName.c_str(),
            shaderMacros,
            true,
            true
        );

        uint32_t stageIndex = 0;
        if (enumHasAnyFlags(mModuleFlags, ModuleFlags::Vertex))
        {
            std::vector<uint32_t> vertexSpirv = std::move(spirvs[stageIndex++]);
            result.vertexModule = new vk::ShaderModule(mDevice, vertexSpirv);
            if (vkSetDebugUtilsObjectNameEXT)
            {
                VkDebugUtilsObjectNameInfoEXT nameInfo = {};
                nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
                nameInfo.objectType = VK_OBJECT_TYPE_SHADER_MODULE;
                nameInfo.objectHandle = uint64_t(result.vertexModule->getHandle());
                std::string name = std::string(mName.toStringView()) + "_Vertex";
                nameInfo.pObjectName = name.c_str();
                vkSetDebugUtilsObjectNameEXT(mDevice.getHandle(), &nameInfo);
            }
        }

        if (enumHasAnyFlags(mModuleFlags, ModuleFlags::Fragment))
        {
            std::vector<uint32_t> fragmentSpirv = std::move(spirvs[stageIndex++]);
            result.fragmentModule = new vk::ShaderModule(mDevice, fragmentSpirv);
            if (vkSetDebugUtilsObjectNameEXT)
            {
                VkDebugUtilsObjectNameInfoEXT nameInfo = {};
                nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
                nameInfo.objectType = VK_OBJECT_TYPE_SHADER_MODULE;
                nameInfo.objectHandle = uint64_t(result.fragmentModule->getHandle());
                std::string name = std::string(mName.toStringView()) + "_Fragment";
                nameInfo.pObjectName = name.c_str();
                vkSetDebugUtilsObjectNameEXT(mDevice.getHandle(), &nameInfo);
            }
        }

        if (enumHasAnyFlags(mModuleFlags, ModuleFlags::Compute))
        {
            std::vector<uint32_t> computeSpirv = std::move(spirvs[stageIndex++]);
            result.computeModule = new vk::ShaderModule(mDevice, computeSpirv);
            if (vkSetDebugUtilsObjectNameEXT)
            {
                VkDebugUtilsObjectNameInfoEXT nameInfo = {};
                nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
                nameInfo.objectType = VK_OBJECT_TYPE_SHADER_MODULE;
                nameInfo.objectHandle = uint64_t(result.computeModule->getHandle());
                std::string name = std::string(mName.toStringView()) + "_Compute";
                nameInfo.pObjectName = name.c_str();
                vkSetDebugUtilsObjectNameEXT(mDevice.getHandle(), &nameInfo);
            }
        }

        return result;
    }

}
