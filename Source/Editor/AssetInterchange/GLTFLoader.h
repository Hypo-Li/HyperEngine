#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include <Core/Common/Refable.h>
#include <Engine/Animation/AnimationClip.h>
#include <Engine/Material/MaterialInstance.h>
#include <Engine/Material/MaterialTemplate.h>
#include <Engine/Mesh/StaticMesh.h>
#include <Engine/Mesh/SkeletalMesh.h>
#include <Engine/Skeleton/Skeleton.h>

namespace tinygltf
{
    class Model;
    class Node;
    struct Mesh;
    struct Primitive;
    struct Material;
    struct Skin;
    struct Animation;
}

namespace hyp::ed
{
    struct GLTFLoadResult
    {
        std::vector<TRef<StaticMesh>> meshes;           // 每个不带 skin 的 glTF mesh 一个(可能含多个 primitive/submesh)
        std::vector<TRef<SkeletalMesh>> skeletalMeshes; // 每个带 skin 的 glTF mesh 一个
        std::vector<TRef<Skeleton>> skeletons;          // 每个 glTF skin 一个(按需去重, 见 load() 里的说明)
        std::vector<TRef<Texture2D>> textures;         // 只包含被材质实际引用到的贴图
        std::vector<TRef<MaterialInstance>> materials; // 每个 glTF material 一个
        // 每个 glTF animation 一个——只有绑定到某个已导入骨骼的 channel(target_path 为
        // translation/rotation/scale)才会生成 BoneTrack, 其余(动网格/相机/灯光节点本身、
        // morph target 用的 weights)直接丢弃; 一个 animation 里所有 channel 都被丢弃时,
        // 不产出这个 AnimationClip(见 GLTFLoader::importAnimation() 的说明)。
        std::vector<TRef<AnimationClip>> animations;

        // 分别跟 meshes/skeletalMeshes/skeletons/textures/materials/animations 一一对应,
        // 落盘时给资产命名用——取 glTF 里对应对象自带的 name 字段, 缺失时是 "Mesh"/"Texture"/
        // "Material"/"Animation" + 下标这种兜底名字。只保证非空, 不保证互不相同(glTF 本身
        // 不要求 name 唯一)——重名交给落盘那一步的去重逻辑处理。
        std::vector<std::string> meshNames;
        std::vector<std::string> skeletalMeshNames;
        std::vector<std::string> skeletonNames;
        std::vector<std::string> textureNames;
        std::vector<std::string> materialNames;
        std::vector<std::string> animationNames;
    };

    // 把 glTF 2.0 文件(.gltf/.glb)解析成引擎自己的资产对象——只生产内存中的 TRef 对象,
    // 不调用 AssetManager 落盘(存不存、存哪由调用方决定), 也不还原 glTF 的 node 层级/
    // 实例变换(不碰 Scene/Entity), 只是把文件里的 mesh/texture/material 转换成引擎资产。
    //
    // 材质转换用的是引擎内置资产 Engine/Materials/GLTFMaterial 这份 MaterialTemplate——glTF 的
    // metallicRoughness 材质模型和这份模板的参数/Slang 着色函数是固定映射关系, 每个 glTF
    // material 只是这份模板的一个 MaterialInstance, 覆盖各自的参数值, 不需要为每个材质单独
    // 生成 Slang 着色源码。importMaterials 为 false 时, result.materials/materialNames 保持
    // 为空(不产出任何独立材质资产), 每个 Mesh 的材质槽统一直接引用 Engine/Materials/
    // WorldGridMaterial 这份引擎内置资产本身。同一个 Mesh 内, 相同 glTF materialIndex 的
    // 子网格共用同一个材质槽(按 materialIndex 分槽), 保留原有材质分组数量, 但不跨 Mesh 共享。
    class GLTFLoader
    {
    public:
        GLTFLoader();

        ~GLTFLoader();

        std::optional<GLTFLoadResult> load(
            const std::filesystem::path& path,
            bool importMaterials = true,
            bool importSkinning = true,
            bool importAnimations = true
        );

    private:
        MaterialTemplate* getGltfPbrTemplate();

        MaterialTemplate* getWorldGridTemplate();

        // key = (glTF 图片索引, 是否法线贴图, isSRGB), 同一个 load() 调用内按需懒创建、去重
        TRef<Texture2D> getOrImportTexture(
            const tinygltf::Model& model,
            int gltfTextureIndex,
            bool isSRGB,
            CompressionPreset preset,
            std::unordered_map<uint64_t, TRef<Texture2D>>& textureCache,
            std::vector<TRef<Texture2D>>& outTextures,
            std::vector<std::string>& outTextureNames
        );

        TRef<MaterialInstance> importMaterial(
            const tinygltf::Model& model,
            const tinygltf::Material& gltfMaterial,
            std::unordered_map<uint64_t, TRef<Texture2D>>& textureCache,
            std::vector<TRef<Texture2D>>& outTextures,
            std::vector<std::string>& outTextureNames
        );

        TRef<StaticMesh> importStaticMesh(
            const tinygltf::Model& model,
            const tinygltf::Mesh& gltfMesh,
            std::span<const TRef<MaterialInstance>> importedMaterials,
            bool importMaterials
        );

        // jointIndexRemap 是 skin.joints 数组下标(JOINTS_0 顶点属性里的索引就是这个)到
        // 对应 Skeleton 里骨骼下标的映射——importSkeleton() 为了保证父骨骼排在子骨骼前面
        // (Skeleton::addBone() 的拓扑序要求), 内部按 BFS 重新分配过下标, 不一定跟 skin.joints
        // 原始顺序一致
        TRef<SkeletalMesh> importSkeletalMesh(
            const tinygltf::Model& model,
            const tinygltf::Mesh& gltfMesh,
            Skeleton* skeleton,
            std::span<const uint32_t> jointIndexRemap,
            std::span<const TRef<MaterialInstance>> importedMaterials,
            bool importMaterials
        );

        // outJointIndexRemap 输出 skin.joints 下标到 Skeleton 骨骼下标的映射(见
        // importSkeletalMesh() 的参数说明), 大小等于 gltfSkin.joints.size()。
        // outRootAncestorMatrices: 根骨骼(在 joint 集合里找不到父)如果头上还挂着非 joint 的
        // 祖先节点(比如 Blender 导出时场景最上层缩放不是 1 的空物体), 这层变换已经被烘焙进
        // 这根骨骼自己的 bind pose 局部 TRS 里, 但动画数据(如果覆盖了这根骨骼的关键帧)不会
        // 跟着一起被烘焙——这里额外把这根骨骼名字对应的祖先矩阵记下来, 交给 importAnimation()
        // 在解析这根骨骼的动画关键帧时做同样的修正, 见该函数的说明
        TRef<Skeleton> importSkeleton(
            const tinygltf::Model& model,
            const tinygltf::Skin& gltfSkin,
            std::vector<uint32_t>& outJointIndexRemap,
            std::unordered_map<HString, float4x4>& outRootAncestorMatrices
        );

        // boneNameByNode: glTF node 下标 -> 已导入骨骼的名字, 只覆盖真正是某个 skin 关节的
        // node(见 load() 里的说明)——channel 目标不在这张表里就跳过, 不产出对应的 BoneTrack。
        // rootAncestorMatrices: importSkeleton() 输出的、按骨骼名字索引的祖先矩阵(只有带
        // 非 joint 祖先的根骨骼才会出现在这张表里)——这根骨骼的 translation/scale 关键帧要用
        // 同一个矩阵修正, 否则骨架 bind pose 里已经烘焙的那层缩放/偏移不会体现在动画数据上,
        // 动画一旦覆盖这根骨骼的姿态就会跟 bind pose 差出这一层没修正的变换(表现为位置爆炸)
        TRef<AnimationClip> importAnimation(
            const tinygltf::Model& model,
            const tinygltf::Animation& gltfAnimation,
            const std::unordered_map<int, HString>& boneNameByNode,
            const std::unordered_map<HString, float4x4>& rootAncestorMatrices
        );

    private:
        TRef<MaterialTemplate> mGltfPbrMaterialTemplate;
        TRef<MaterialTemplate> mWorldGridMaterialTemplate;
    };
}
