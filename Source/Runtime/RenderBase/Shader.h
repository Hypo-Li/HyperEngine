#pragma once
#include <filesystem>

#include "Vulkan/Pipeline.h"
#include "VertexLayout.h"
#include "RenderTagRegistry.h"

#include <Core/Common/HString.h>

namespace hyp
{
    struct ShaderVariant
    {
        TRef<vk::ShaderModule> vertexModule;
        TRef<vk::ShaderModule> fragmentModule;
        TRef<vk::ShaderModule> computeModule;

        bool operator==(const ShaderVariant& other) const noexcept
        {
            return vertexModule == other.vertexModule
                && fragmentModule == other.fragmentModule
                && computeModule == other.computeModule;
        }
    };


    struct ShaderMacro
    {
        HString name;
        HString value;

        ShaderMacro(HString n, HString v = "") : name(n), value(v) {}

        bool operator==(const ShaderMacro& other) const noexcept
        {
            return name == other.name && value == other.value;
        }
    };

}

namespace std
{
    template <>
    struct hash<hyp::ShaderVariant>
    {
        size_t operator()(const hyp::ShaderVariant& variant) const noexcept
        {
            size_t seed = 0;
            hyp::Hash::combine(seed, variant.vertexModule.get());
            hyp::Hash::combine(seed, variant.fragmentModule.get());
            hyp::Hash::combine(seed, variant.computeModule.get());
            return seed;
        }
    };

    template <>
    struct hash<std::vector<hyp::ShaderMacro>>
    {
        size_t operator()(const std::vector<hyp::ShaderMacro>& macros) const noexcept
        {
            size_t seed = 0;
            for (auto& macro : macros)
            {
                size_t nameHash = macro.name.getTypeHash();
                size_t valueHash = macro.value.getTypeHash();
                hyp::Hash::combine(seed, nameHash);
                hyp::Hash::combine(seed, valueHash);
            }
            return seed;
        }
    };
}

namespace hyp
{
    class MaterialShader
    {
    public:
        MaterialShader(
            const vk::Device& device,
            RenderTagRegistry& renderTagRegistry,
            HString name,
            std::string materialSource
        );

        ShaderVariant getOrCreateVariant(HString renderTag, VertexLayout vertexLayout, std::span<const ShaderMacro> shaderMacros);

    private:
        ShaderVariant createVariant(HString renderTag, VertexLayout vertexLayout, std::span<const ShaderMacro> shaderMacros);

        std::string makeShaderSource(HString renderTag, VertexLayout vertexLayout);

    private:
        const vk::Device& mDevice;
        RenderTagRegistry& mRenderTagRegistry;
        HString mName;
        std::string mMaterialSource;

        struct VariantKey
        {
            HString renderTag;
            VertexLayout vertexLayout;

            bool operator==(const VariantKey& other) const noexcept;
        };

        struct VariantKeyHash
        {
            std::size_t operator()(const VariantKey& key) const noexcept;
        };

        std::unordered_map<VariantKey, ShaderVariant, VariantKeyHash> mVariants;
    };

    class MaterialShaderHandle
    {
    public:
        MaterialShaderHandle() = default;

        MaterialShaderHandle(uint32_t index, uint32_t version)
            : mIndex(index), mVersion(version)
        {
        }

        bool operator==(const MaterialShaderHandle& other) const noexcept
        {
            return mIndex == other.mIndex && mVersion == other.mVersion;
        }

        uint32_t getIndex() const noexcept
        {
            return mIndex;
        }

        uint32_t getVersion() const noexcept
        {
            return mVersion;
        }

        bool isValid() const noexcept
        {
            return mIndex != ~0u && mVersion != ~0u;
        }

    private:
        uint32_t mIndex = ~0u;
        uint32_t mVersion = ~0u;
    };

    class MaterialShaderManager
    {
    public:
        MaterialShaderManager(const vk::Device& device, RenderTagRegistry& renderTagRegistry);

        // Logic thread methods:

        MaterialShaderHandle allocateHandle();

        void freeHandle(MaterialShaderHandle handle);

        // Render thread methods:

        void createMaterialShader(MaterialShaderHandle handle, HString name, const std::string& materialSource);

        void refMaterialShader(MaterialShaderHandle handle);

        void unrefMaterialShader(MaterialShaderHandle handle);

        MaterialShader* getMaterialShader(MaterialShaderHandle handle) const;

    private:
        const vk::Device& mDevice;
        RenderTagRegistry& mRenderTagRegistry;
        std::vector<uint32_t> mVersions;
        std::vector<uint32_t> mFreeIndices;
        struct Slot
        {
            std::unique_ptr<MaterialShader> shader;
            uint32_t refCount = 0;
            uint32_t version = 0;
        };
        std::vector<Slot> mSlots;
    };

    class PassShader
    {
    public:
        PassShader(const vk::Device& device, HString name, const std::filesystem::path& shaderPath);

        ShaderVariant getOrCreateVariant(std::span<const ShaderMacro> shaderMacros);

    private:
        ShaderVariant createVariant(std::span<const ShaderMacro> shaderMacros);

    private:
        const vk::Device& mDevice;
        HString mName;
        std::string mShaderFileName;
        std::string mShaderSource;

        enum class ModuleFlags
        {
            None = 0,
            Vertex = 1 << 0,
            Fragment = 1 << 1,
            Compute = 1 << 2,
        };
        ENABLE_ENUM_CLASS_BITWISE_FRIEND(ModuleFlags)
        ModuleFlags mModuleFlags = ModuleFlags::None;

        std::unordered_map<std::vector<ShaderMacro>, ShaderVariant> mVariants;
    };

    ENABLE_ENUM_CLASS_BITWISE(PassShader::ModuleFlags)
}
