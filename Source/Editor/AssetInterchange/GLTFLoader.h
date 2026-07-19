#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include <Core/Common/Refable.h>

namespace tinygltf
{
    class Model;
    struct Mesh;
    struct Primitive;
    struct Material;
}

namespace hyp
{
    class Mesh;
    class Texture2D;
    class MaterialTemplate;
    class MaterialInstance;
    enum class CompressionPreset : uint8_t;
}

namespace hyp::editor
{
    struct GLTFLoadResult
    {
        std::vector<TRef<Mesh>> meshes;                // 每个 glTF mesh 一个(可能含多个 primitive/submesh)
        std::vector<TRef<Texture2D>> textures;         // 只包含被材质实际引用到的贴图
        std::vector<TRef<MaterialInstance>> materials; // 每个 glTF material 一个
    };

    // 把 glTF 2.0 文件(.gltf/.glb)解析成引擎自己的资产对象——只生产内存中的 TRef 对象,
    // 不调用 AssetManager 落盘(存不存、存哪由调用方决定), 也不还原 glTF 的 node 层级/
    // 实例变换(不碰 Scene/Entity), 只是把文件里的 mesh/texture/material 转换成引擎资产。
    //
    // 材质转换用的是同一份内部维护的"glTF PBR" MaterialTemplate(懒创建, 生命周期跟随
    // GLTFLoader 实例)——glTF 的 metallicRoughness 材质模型和这份模板的参数/Slang 着色函数
    // 是固定映射关系, 每个 glTF material 只是这份模板的一个 MaterialInstance, 覆盖各自的
    // 参数值, 不需要为每个材质单独生成 Slang 源码
    class GLTFLoader
    {
    public:
        GLTFLoader();

        ~GLTFLoader();

        std::optional<GLTFLoadResult> load(const std::filesystem::path& path);

    private:
        MaterialTemplate* getOrCreateGltfPbrTemplate();

        Texture2D* getOrCreateDefaultWhiteTexture();

        Texture2D* getOrCreateDefaultFlatNormalTexture();

        // key = (glTF 图片索引, isSRGB), 同一个 load() 调用内按需懒创建、去重
        TRef<Texture2D> getOrImportTexture(
            const tinygltf::Model& model,
            int gltfTextureIndex,
            bool isSRGB,
            CompressionPreset preset,
            std::unordered_map<uint64_t, TRef<Texture2D>>& textureCache,
            std::vector<TRef<Texture2D>>& outTextures
        );

        TRef<MaterialInstance> importMaterial(
            const tinygltf::Model& model,
            const tinygltf::Material& gltfMaterial,
            std::unordered_map<uint64_t, TRef<Texture2D>>& textureCache,
            std::vector<TRef<Texture2D>>& outTextures
        );

        TRef<Mesh> importMesh(
            const tinygltf::Model& model,
            const tinygltf::Mesh& gltfMesh,
            std::span<const TRef<MaterialInstance>> importedMaterials
        );

    private:
        TRef<MaterialTemplate> mGltfPbrMaterialTemplate;
        TRef<Texture2D> mDefaultWhiteTexture;
        TRef<Texture2D> mDefaultFlatNormalTexture;
    };
}
