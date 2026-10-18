#include "GLTFLoader.h"

// AssetInterchange 是唯一链接 tinygltf 的模块, 实现宏在这一份 .cpp 里定义一次——
// 多个 .cpp 都定义会导致重复符号链接错误
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include <tiny_gltf.h>

#include <Core/Asset/Asset.h>
#include <Core/Asset/AssetManager.h>
#include <Core/Common/LogSystem.h>
#include <Core/Math/Vector.h>
#include <Core/Object/Object.h>
#include <Engine/Context.h>
#include <Engine/Material/MaterialInstance.h>
#include <Engine/Material/MaterialTemplate.h>
#include <Engine/Texture/SampleState.h>
#include <Engine/Texture/Texture2D.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

#include <cassert>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace hyp::ed
{
    namespace
    {
        // 读一个 accessor 的所有分量, 统一转成 float——FLOAT 原样, 归一化的 UBYTE/USHORT
        // 按 glTF 规范除以 255/65535。expectedComponentCount 是调用方期望的分量数
        // (2=VEC2, 3=VEC3, 4=VEC4), 和 accessor 实际类型对不上就说明数据不合规范, 直接跳过
        std::vector<float> readAccessorFloats(const tinygltf::Model& model, int accessorIndex, int expectedComponentCount)
        {
            if (accessorIndex < 0 || static_cast<size_t>(accessorIndex) >= model.accessors.size())
                return {};

            const tinygltf::Accessor& accessor = model.accessors[accessorIndex];
            if (tinygltf::GetNumComponentsInType(static_cast<uint32_t>(accessor.type)) != expectedComponentCount)
                return {};
            if (accessor.bufferView < 0 || static_cast<size_t>(accessor.bufferView) >= model.bufferViews.size())
                return {};

            const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
            const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];

            int32_t componentSize = tinygltf::GetComponentSizeInBytes(static_cast<uint32_t>(accessor.componentType));
            size_t elementSize = static_cast<size_t>(componentSize) * expectedComponentCount;
            size_t stride = bufferView.byteStride ? bufferView.byteStride : elementSize;
            const uint8_t* base = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;

            std::vector<float> result(accessor.count * expectedComponentCount);
            for (size_t i = 0; i < accessor.count; ++i)
            {
                const uint8_t* element = base + stride * i;
                for (int c = 0; c < expectedComponentCount; ++c)
                {
                    float value = 0.0f;
                    switch (accessor.componentType)
                    {
                    case TINYGLTF_COMPONENT_TYPE_FLOAT:
                        value = reinterpret_cast<const float*>(element)[c];
                        break;
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                        value = element[c] / 255.0f;
                        break;
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                        value = reinterpret_cast<const uint16_t*>(element)[c] / 65535.0f;
                        break;
                    default:
                        break;
                    }
                    result[i * expectedComponentCount + c] = value;
                }
            }
            return result;
        }

        // glTF 规范定义的坐标系是右手系、+Y 朝上、+Z 指向观察者(即 -Z 是"看向场景"的前方);
        // 引擎是右手系、+Z 朝上、+Y 为前方、+X 为右方(见 Transform::UpVector 等约定)。两边
        // 都是右手系, 只是绕 X 轴转了 90 度, 不需要额外镜像翻转手性——(x,y,z)_gltf 映射到
        // (x, -z, y)_engine: 右方分量不变, glTF 的"朝上"变成引擎的"朝上"(Z), glTF 的
        // "-Z 前方"变成引擎的"+Y 前方"。位置、法线、切线(仅 xyz, w 分量是决定副切线方向的
        // 手性符号, 纯旋转不改变手性, 不需要跟着翻)都要经过这同一个变换才能对齐引擎坐标系,
        // 不做的话导入进来的网格会"立"在 XY 平面上、朝向也全部对不上。
        float3 gltfToEngineAxis(const float3& v) noexcept
        {
            return float3(v.x, -v.z, v.y);
        }

        std::vector<float3> readVec3(const tinygltf::Model& model, int accessorIndex)
        {
            std::vector<float> raw = readAccessorFloats(model, accessorIndex, 3);
            std::vector<float3> result(raw.size() / 3);
            for (size_t i = 0; i < result.size(); ++i)
                result[i] = float3(raw[i * 3], raw[i * 3 + 1], raw[i * 3 + 2]);
            return result;
        }

        std::vector<float4> readVec4(const tinygltf::Model& model, int accessorIndex)
        {
            std::vector<float> raw = readAccessorFloats(model, accessorIndex, 4);
            std::vector<float4> result(raw.size() / 4);
            for (size_t i = 0; i < result.size(); ++i)
                result[i] = float4(raw[i * 4], raw[i * 4 + 1], raw[i * 4 + 2], raw[i * 4 + 3]);
            return result;
        }

        std::vector<float2> readVec2(const tinygltf::Model& model, int accessorIndex)
        {
            std::vector<float> raw = readAccessorFloats(model, accessorIndex, 2);
            std::vector<float2> result(raw.size() / 2);
            for (size_t i = 0; i < result.size(); ++i)
                result[i] = float2(raw[i * 2], raw[i * 2 + 1]);
            return result;
        }

        // COLOR_0 既可能是 VEC3(缺 alpha 补 1)也可能是 VEC4
        std::vector<float4> readColor(const tinygltf::Model& model, int accessorIndex)
        {
            if (accessorIndex < 0 || static_cast<size_t>(accessorIndex) >= model.accessors.size())
                return {};

            int numComponents = tinygltf::GetNumComponentsInType(static_cast<uint32_t>(model.accessors[accessorIndex].type));
            if (numComponents == 4)
                return readVec4(model, accessorIndex);
            if (numComponents == 3)
            {
                std::vector<float3> rgb = readVec3(model, accessorIndex);
                std::vector<float4> result(rgb.size());
                for (size_t i = 0; i < rgb.size(); ++i)
                    result[i] = float4(rgb[i], 1.0f);
                return result;
            }
            return {};
        }

        // 索引统一读成 uint32_t——最终存进 Mesh 时再根据 needsUint32 决定是压回 Uint16 还是
        // 保持 Uint32, 这里先不丢精度
        std::vector<uint32_t> readIndices(const tinygltf::Model& model, int accessorIndex, bool& outNeedsUint32)
        {
            std::vector<uint32_t> result;
            if (accessorIndex < 0 || static_cast<size_t>(accessorIndex) >= model.accessors.size())
                return result;

            const tinygltf::Accessor& accessor = model.accessors[accessorIndex];
            if (accessor.bufferView < 0 || static_cast<size_t>(accessor.bufferView) >= model.bufferViews.size())
                return result;

            const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
            const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];

            int32_t componentSize = tinygltf::GetComponentSizeInBytes(static_cast<uint32_t>(accessor.componentType));
            size_t stride = bufferView.byteStride ? bufferView.byteStride : static_cast<size_t>(componentSize);
            const uint8_t* base = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;

            result.resize(accessor.count);
            for (size_t i = 0; i < accessor.count; ++i)
            {
                const uint8_t* element = base + stride * i;
                switch (accessor.componentType)
                {
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                    result[i] = element[0];
                    break;
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                    result[i] = *reinterpret_cast<const uint16_t*>(element);
                    break;
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                    result[i] = *reinterpret_cast<const uint32_t*>(element);
                    outNeedsUint32 = true;
                    break;
                default:
                    break;
                }
            }
            return result;
        }

        bool computeTriangleTangentFrame(
            const float3& p0, const float3& p1, const float3& p2,
            const float2& uv0, const float2& uv1, const float2& uv2,
            float3& outTangent, float3& outBitangent
        )
        {
            float3 edge1 = p1 - p0;
            float3 edge2 = p2 - p0;
            float2 deltaUV1 = uv1 - uv0;
            float2 deltaUV2 = uv2 - uv0;

            float denom = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;
            if (std::abs(denom) < 1e-8f)
                return false;
            float f = 1.0f / denom;

            outTangent = f * (deltaUV2.y * edge1 - deltaUV1.y * edge2);
            outBitangent = f * (deltaUV1.x * edge2 - deltaUV2.x * edge1);
            return true;
        }

        // 缺 TANGENT 时按标准的逐三角形累加 + Gram-Schmidt 正交化 + 叉乘定符号算切线,
        // 移植自 Source/Test/PBR/Mesh.cpp 的 calculateTangents()
        void calculateTangents(
            std::span<const float3> positions,
            std::span<const float3> normals,
            std::span<const float2> texCoord0s,
            std::span<const uint32_t> indices,
            std::span<float4> outTangents
        )
        {
            size_t vertexCount = positions.size();
            std::vector<float3> tan1(vertexCount, float3(0.0f));
            std::vector<float3> tan2(vertexCount, float3(0.0f));

            for (size_t i = 0; i + 2 < indices.size(); i += 3)
            {
                uint32_t i0 = indices[i];
                uint32_t i1 = indices[i + 1];
                uint32_t i2 = indices[i + 2];

                float3 tangent;
                float3 bitangent;
                if (!computeTriangleTangentFrame(
                    positions[i0], positions[i1], positions[i2],
                    texCoord0s[i0], texCoord0s[i1], texCoord0s[i2],
                    tangent, bitangent))
                    continue;

                tan1[i0] += tangent; tan1[i1] += tangent; tan1[i2] += tangent;
                tan2[i0] += bitangent; tan2[i1] += bitangent; tan2[i2] += bitangent;
            }

            for (size_t i = 0; i < vertexCount; ++i)
            {
                const float3& n = normals[i];
                float3 t = tan1[i];
                t = t - n * glm::dot(n, t);
                float lengthSqr = glm::dot(t, t);
                if (lengthSqr < 1e-12f)
                {
                    // 退化情况(比如这个顶点没被任何三角形正确累加到)——随便挑一个和法线
                    // 垂直的方向兜底, 保证不产生 NaN
                    t = std::abs(n.y) > 0.999f ? float3(1, 0, 0) : glm::normalize(glm::cross(float3(0, 1, 0), n));
                }
                else
                {
                    t = glm::normalize(t);
                }
                float w = (glm::dot(glm::cross(n, t), tan2[i]) < 0.0f) ? -1.0f : 1.0f;
                outTangents[i] = float4(t, w);
            }
        }

        // glTF 规范的 w 与引擎(DX 法线贴图)约定相反, 多数顶点违反引擎约定时整体翻转。
        void normalizeTangentHandedness(
            std::span<const float3> positions,
            std::span<const float3> normals,
            std::span<const float2> texCoord0s,
            std::span<const uint32_t> indices,
            std::span<float4> tangents
        )
        {
            size_t agreeCount = 0;
            size_t opposeCount = 0;
            for (size_t i = 0; i + 2 < indices.size(); i += 3)
            {
                float3 tangent;
                float3 bitangent;
                if (!computeTriangleTangentFrame(
                    positions[indices[i]], positions[indices[i + 1]], positions[indices[i + 2]],
                    texCoord0s[indices[i]], texCoord0s[indices[i + 1]], texCoord0s[indices[i + 2]],
                    tangent, bitangent))
                    continue;

                for (size_t c = 0; c < 3; ++c)
                {
                    uint32_t vertex = indices[i + c];
                    const float4& t = tangents[vertex];
                    float alignment = glm::dot(glm::cross(normals[vertex], float3(t)) * t.w, bitangent);
                    if (alignment > 0.0f)
                        ++agreeCount;
                    else if (alignment < 0.0f)
                        ++opposeCount;
                }
            }

            if (opposeCount > agreeCount)
            {
                for (float4& t : tangents)
                    t.w = -t.w;
            }
        }

        // gltfToEngineAxis(float3) 描述的坐标变换等价于绕 gltf X 轴 +90°(WXYZ 四元数
        // (√2/2, √2/2, 0, 0)), 旋转量要按 q' = C q C⁻¹ 共轭一次才能对齐引擎坐标系
        Quaternion gltfToEngineAxis(const Quaternion& q) noexcept
        {
            static const Quaternion kAxisRotation(std::sqrt(0.5f), std::sqrt(0.5f), 0.0f, 0.0f);
            return kAxisRotation * q * glm::conjugate(kAxisRotation);
        }

        struct LocalTRS
        {
            float3 position{ 0, 0, 0 };
            Quaternion rotation{ 1, 0, 0, 0 };
            float3 scale{ 1, 1, 1 };
        };

        // glTF 节点要么用 matrix, 要么用 translation/rotation/scale, 两者不会同时出现——
        // 都缺失时是单位变换。返回值仍是 glTF 坐标系下的值, 轴变换由调用方做
        LocalTRS readNodeLocalTRS(const tinygltf::Node& node)
        {
            LocalTRS trs;
            if (node.matrix.size() == 16)
            {
                glm::dmat4 m;
                std::memcpy(&m, node.matrix.data(), sizeof(double) * 16);
                glm::dvec3 scale, translation, skew;
                glm::dvec4 perspective;
                glm::dquat rotation;
                glm::decompose(m, scale, rotation, translation, skew, perspective);
                trs.position = float3(translation);
                trs.rotation = Quaternion(static_cast<float>(rotation.w), static_cast<float>(rotation.x), static_cast<float>(rotation.y), static_cast<float>(rotation.z));
                trs.scale = float3(scale);
                return trs;
            }
            if (node.translation.size() == 3)
                trs.position = float3(node.translation[0], node.translation[1], node.translation[2]);
            if (node.rotation.size() == 4)
                trs.rotation = Quaternion(static_cast<float>(node.rotation[3]), static_cast<float>(node.rotation[0]), static_cast<float>(node.rotation[1]), static_cast<float>(node.rotation[2]));
            if (node.scale.size() == 3)
                trs.scale = float3(node.scale[0], node.scale[1], node.scale[2]);
            return trs;
        }

        // JOINTS_0 存的是骨骼索引(UBYTE/USHORT, 不能像 WEIGHTS_0 那样按 readVec4 归一化)
        std::vector<uint4> readJoints(const tinygltf::Model& model, int accessorIndex)
        {
            if (accessorIndex < 0 || static_cast<size_t>(accessorIndex) >= model.accessors.size())
                return {};

            const tinygltf::Accessor& accessor = model.accessors[accessorIndex];
            if (tinygltf::GetNumComponentsInType(static_cast<uint32_t>(accessor.type)) != 4)
                return {};
            if (accessor.bufferView < 0 || static_cast<size_t>(accessor.bufferView) >= model.bufferViews.size())
                return {};

            const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
            const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];

            int32_t componentSize = tinygltf::GetComponentSizeInBytes(static_cast<uint32_t>(accessor.componentType));
            size_t elementSize = static_cast<size_t>(componentSize) * 4;
            size_t stride = bufferView.byteStride ? bufferView.byteStride : elementSize;
            const uint8_t* base = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;

            std::vector<uint4> result(accessor.count);
            for (size_t i = 0; i < accessor.count; ++i)
            {
                const uint8_t* element = base + stride * i;
                uint32_t values[4] = {};
                for (int c = 0; c < 4; ++c)
                {
                    switch (accessor.componentType)
                    {
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                        values[c] = element[c];
                        break;
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                        values[c] = reinterpret_cast<const uint16_t*>(element)[c];
                        break;
                    default:
                        break;
                    }
                }
                result[i] = uint4(values[0], values[1], values[2], values[3]);
            }
            return result;
        }

        struct GLTFPrimitiveData
        {
            std::vector<float3> positions;
            std::vector<float3> normals;
            std::vector<float4> tangents;
            std::vector<float2> texCoord0s;
            std::vector<float4> colors;  // 为空表示这个 primitive 没有 COLOR_0
            std::vector<uint4> joints;   // 为空表示没有读 JOINTS_0(非蒙皮导入, 或蒙皮属性缺失/顶点数对不上)
            std::vector<float4> weights; // 为空表示没有读 WEIGHTS_0, 情况同上
            std::vector<uint32_t> indices;
            bool needsUint32Index = false;
            int materialIndex = -1;
        };

        // JOINTS_0/WEIGHTS_0 缺一, 或顶点数跟 POSITION 对不上, 都视为没有蒙皮数据(joints/weights 留空)
        std::optional<GLTFPrimitiveData> readGLTFPrimitive(const tinygltf::Model& model, const tinygltf::Primitive& gltfPrimitive, bool readSkinning)
        {
            if (gltfPrimitive.mode != TINYGLTF_MODE_TRIANGLES)
                return std::nullopt;

            auto positionIt = gltfPrimitive.attributes.find("POSITION");
            if (positionIt == gltfPrimitive.attributes.end() || gltfPrimitive.indices < 0)
                return std::nullopt;

            GLTFPrimitiveData primitive;
            primitive.positions = readVec3(model, positionIt->second);
            if (primitive.positions.empty())
                return std::nullopt;
            size_t vertexCount = primitive.positions.size();
            for (float3& position : primitive.positions)
                position = gltfToEngineAxis(position);

            auto normalIt = gltfPrimitive.attributes.find("NORMAL");
            primitive.normals = normalIt != gltfPrimitive.attributes.end()
                ? readVec3(model, normalIt->second)
                : std::vector<float3>(vertexCount, float3(0, 0, 1));
            if (normalIt != gltfPrimitive.attributes.end())
            {
                for (float3& normal : primitive.normals)
                    normal = gltfToEngineAxis(normal);
            }

            auto tangentIt = gltfPrimitive.attributes.find("TANGENT");
            bool hasTangent = tangentIt != gltfPrimitive.attributes.end();
            primitive.tangents = hasTangent
                ? readVec4(model, tangentIt->second)
                : std::vector<float4>(vertexCount, float4(1, 0, 0, 1));
            if (hasTangent)
            {
                for (float4& tangent : primitive.tangents)
                {
                    float3 converted = gltfToEngineAxis(float3(tangent.x, tangent.y, tangent.z));
                    tangent = float4(converted, tangent.w);
                }
            }

            auto texCoordIt = gltfPrimitive.attributes.find("TEXCOORD_0");
            primitive.texCoord0s = texCoordIt != gltfPrimitive.attributes.end()
                ? readVec2(model, texCoordIt->second)
                : std::vector<float2>(vertexCount, float2(0, 0));

            auto colorIt = gltfPrimitive.attributes.find("COLOR_0");
            if (colorIt != gltfPrimitive.attributes.end())
                primitive.colors = readColor(model, colorIt->second);

            if (readSkinning)
            {
                auto jointsIt = gltfPrimitive.attributes.find("JOINTS_0");
                auto weightsIt = gltfPrimitive.attributes.find("WEIGHTS_0");
                if (jointsIt != gltfPrimitive.attributes.end() && weightsIt != gltfPrimitive.attributes.end())
                {
                    primitive.joints = readJoints(model, jointsIt->second);
                    primitive.weights = readVec4(model, weightsIt->second);
                    if (primitive.joints.size() != vertexCount || primitive.weights.size() != vertexCount)
                    {
                        primitive.joints.clear();
                        primitive.weights.clear();
                    }
                }
            }

            primitive.indices = readIndices(model, gltfPrimitive.indices, primitive.needsUint32Index);
            if (primitive.indices.empty())
                return std::nullopt;

            if (!hasTangent && primitive.normals.size() == vertexCount && primitive.texCoord0s.size() == vertexCount)
                calculateTangents(primitive.positions, primitive.normals, primitive.texCoord0s, primitive.indices, primitive.tangents);

            const bool hasNormalAndUV = normalIt != gltfPrimitive.attributes.end() && texCoordIt != gltfPrimitive.attributes.end();
            if (hasTangent && hasNormalAndUV && primitive.tangents.size() == vertexCount && primitive.texCoord0s.size() == vertexCount)
                normalizeTangentHandedness(primitive.positions, primitive.normals, primitive.texCoord0s, primitive.indices, primitive.tangents);

            primitive.materialIndex = gltfPrimitive.material;
            return primitive;
        }

        AddressMode convertWrapMode(int gltfWrapMode)
        {
            switch (gltfWrapMode)
            {
            case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:
                return AddressMode::Clamp;
            case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT:
                return AddressMode::Mirror;
            case TINYGLTF_TEXTURE_WRAP_REPEAT:
            default:
                return AddressMode::Repeat;
            }
        }

        BlendMode convertAlphaMode(const std::string& alphaMode)
        {
            if (alphaMode == "MASK")
                return BlendMode::Masked;
            if (alphaMode == "BLEND")
                return BlendMode::Translucent;
            return BlendMode::Opaque;
        }
    }

    GLTFLoader::GLTFLoader() = default;

    GLTFLoader::~GLTFLoader() = default;

    MaterialTemplate* GLTFLoader::getGltfPbrTemplate()
    {
        if (mGltfPbrMaterialTemplate)
            return mGltfPbrMaterialTemplate.get();

        Asset* asset = Context::get().getAssetManager().getAsset("Engine/Materials/GLTFMaterial");
        assert(asset && "Engine/Materials/GLTFMaterial 是引擎内置资产, 缺失说明 Engine 资产目录没有正确部署");
        mGltfPbrMaterialTemplate = asset->getOrLoadRootObject<MaterialTemplate>();
        return mGltfPbrMaterialTemplate.get();
    }

    MaterialTemplate* GLTFLoader::getWorldGridTemplate()
    {
        if (mWorldGridMaterialTemplate)
            return mWorldGridMaterialTemplate.get();

        Asset* asset = Context::get().getAssetManager().getAsset("Engine/Materials/WorldGridMaterial");
        assert(asset && "Engine/Materials/WorldGridMaterial 是引擎内置资产, 缺失说明 Engine 资产目录没有正确部署");
        mWorldGridMaterialTemplate = asset->getOrLoadRootObject<MaterialTemplate>();
        return mWorldGridMaterialTemplate.get();
    }

    TRef<Texture2D> GLTFLoader::getOrImportTexture(
        const tinygltf::Model& model,
        int gltfTextureIndex,
        bool isSRGB,
        CompressionPreset preset,
        std::unordered_map<uint64_t, TRef<Texture2D>>& textureCache,
        std::vector<TRef<Texture2D>>& outTextures,
        std::vector<std::string>& outTextureNames
    )
    {
        if (gltfTextureIndex < 0 || static_cast<size_t>(gltfTextureIndex) >= model.textures.size())
            return nullptr;

        const tinygltf::Texture& gltfTexture = model.textures[gltfTextureIndex];
        if (gltfTexture.source < 0 || static_cast<size_t>(gltfTexture.source) >= model.images.size())
            return nullptr;

        const bool isNormalMap = preset == CompressionPreset::Normal_Map;
        uint64_t cacheKey = (static_cast<uint64_t>(gltfTexture.source) << 2) | (isNormalMap ? 2u : 0u) | (isSRGB ? 1u : 0u);
        auto it = textureCache.find(cacheKey);
        if (it != textureCache.end())
            return it->second;

        const tinygltf::Image& gltfImage = model.images[gltfTexture.source];
        if (gltfImage.image.empty() || gltfImage.width <= 0 || gltfImage.height <= 0)
            return nullptr;

        // tinygltf(经由内置的 stb_image)已经把 PNG/JPEG 解码成按行紧凑排列的像素数据,
        // component 是实际解出来的通道数(1~4)——这里统一转成 4 通道 RGBA8, 免得 3 通道
        // 格式在不同 GPU 上的对齐/支持情况不一致
        int width = gltfImage.width;
        int height = gltfImage.height;
        int srcComponents = gltfImage.component;
        std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
        for (int i = 0; i < width * height; ++i)
        {
            uint8_t r = 0, g = 0, b = 0, a = 255;
            const uint8_t* src = gltfImage.image.data() + static_cast<size_t>(i) * srcComponents;
            switch (srcComponents)
            {
            case 1: r = g = b = src[0]; break;
            case 2: r = g = b = src[0]; a = src[1]; break;
            case 3: r = src[0]; g = src[1]; b = src[2]; break;
            case 4: r = src[0]; g = src[1]; b = src[2]; a = src[3]; break;
            default: break;
            }
            pixels[static_cast<size_t>(i) * 4 + 0] = r;
            pixels[static_cast<size_t>(i) * 4 + 1] = g;
            pixels[static_cast<size_t>(i) * 4 + 2] = b;
            pixels[static_cast<size_t>(i) * 4 + 3] = a;
        }

        TRef<Texture2D> texture = createObject<Texture2D>(nullptr);
        texture->initialize(static_cast<uint32_t>(width), static_cast<uint32_t>(height), PixelFormat::RGBA8_UNORM);
        texture->setSRGB(isSRGB);
        texture->setCompressionPreset(preset);
        texture->setFlipGreenChannel(isNormalMap);
        texture->setSourcePixels(std::move(pixels));
        texture->bakeMips();

        SampleState sampleState;
        sampleState.filter = Filter::Anisotropic;
        if (gltfTexture.sampler >= 0 && static_cast<size_t>(gltfTexture.sampler) < model.samplers.size())
        {
            const tinygltf::Sampler& gltfSampler = model.samplers[gltfTexture.sampler];
            sampleState.addressModeU = convertWrapMode(gltfSampler.wrapS);
            sampleState.addressModeV = convertWrapMode(gltfSampler.wrapT);
        }
        texture->setSampleState(sampleState);

        outTextures.push_back(texture);
        outTextureNames.push_back(!gltfImage.name.empty() ? gltfImage.name : "Texture" + std::to_string(gltfTextureIndex));
        textureCache.emplace(cacheKey, texture);
        return texture;
    }

    TRef<MaterialInstance> GLTFLoader::importMaterial(
        const tinygltf::Model& model,
        const tinygltf::Material& gltfMaterial,
        std::unordered_map<uint64_t, TRef<Texture2D>>& textureCache,
        std::vector<TRef<Texture2D>>& outTextures,
        std::vector<std::string>& outTextureNames
    )
    {
        MaterialTemplate* materialTemplate = getGltfPbrTemplate();

        TRef<MaterialInstance> materialInstance = createObject<MaterialInstance>(nullptr);
        materialInstance->setTemplate(materialTemplate);

        const auto& pbr = gltfMaterial.pbrMetallicRoughness;
        materialInstance->setParameterOverriddenValue(
            "BaseColorFactor",
            float4(
                static_cast<float>(pbr.baseColorFactor[0]),
                static_cast<float>(pbr.baseColorFactor[1]),
                static_cast<float>(pbr.baseColorFactor[2]),
                static_cast<float>(pbr.baseColorFactor[3])
            )
        );
        materialInstance->setParameterOverriddenValue("MetallicFactor", static_cast<float>(pbr.metallicFactor));
        materialInstance->setParameterOverriddenValue("RoughnessFactor", static_cast<float>(pbr.roughnessFactor));
        materialInstance->setParameterOverriddenValue(
            "EmissiveFactor",
            float3(
                static_cast<float>(gltfMaterial.emissiveFactor[0]),
                static_cast<float>(gltfMaterial.emissiveFactor[1]),
                static_cast<float>(gltfMaterial.emissiveFactor[2])
            )
        );

        // baseColor/emissive 是色彩类贴图, 要按 sRGB 解码; metallicRoughness/normal/occlusion
        // 存的是线性数据(法线额外用 Normal_Map 压缩预设), 不能当 sRGB 处理
        if (TRef<Texture2D> texture = getOrImportTexture(model, pbr.baseColorTexture.index, true, CompressionPreset::Default, textureCache, outTextures, outTextureNames))
            materialInstance->setParameterOverriddenValue("BaseColor", texture.get());

        // metallic(B)/roughness(G) 是两个互相独立、高度可见的标量通道(direct 影响高光响应,
        // 条带在这里比在 baseColor 上明显得多)——BC1/BC3 的 RGB 共享端点编码会让两个通道互相
        // 拖累精度, BC7 逐分区独立编码, 是这两个通道能拿到的最好的块压缩选项。
        if (TRef<Texture2D> texture = getOrImportTexture(model, pbr.metallicRoughnessTexture.index, false, CompressionPreset::High_Quality, textureCache, outTextures, outTextureNames))
            materialInstance->setParameterOverriddenValue("MetallicRoughness", texture.get());

        if (TRef<Texture2D> texture = getOrImportTexture(model, gltfMaterial.normalTexture.index, false, CompressionPreset::Normal_Map, textureCache, outTextures, outTextureNames))
        {
            materialInstance->setParameterOverriddenValue("Normal", texture.get());
            materialInstance->setParameterOverriddenValue("NormalScale", static_cast<float>(gltfMaterial.normalTexture.scale));
        }

        // shader 只读 .r(见 GLTFMaterial 材质源码), Grayscale 对应 BC4——单通道格式, 存储只有
        // Default(BC3/BC1)的一半, 没有理由为一张只有一个有效通道的贴图去存 RGBA。
        if (TRef<Texture2D> texture = getOrImportTexture(model, gltfMaterial.occlusionTexture.index, false, CompressionPreset::Grayscale, textureCache, outTextures, outTextureNames))
        {
            materialInstance->setParameterOverriddenValue("Occlusion", texture.get());
            materialInstance->setParameterOverriddenValue("OcclusionStrength", static_cast<float>(gltfMaterial.occlusionTexture.strength));
        }

        if (TRef<Texture2D> texture = getOrImportTexture(model, gltfMaterial.emissiveTexture.index, true, CompressionPreset::Default, textureCache, outTextures, outTextureNames))
            materialInstance->setParameterOverriddenValue("Emissive", texture.get());

        BlendMode blendMode = convertAlphaMode(gltfMaterial.alphaMode);
        materialInstance->setBlendModeOverride(blendMode);
        if (blendMode == BlendMode::Masked)
            materialInstance->setAlphaCutoffOverride(static_cast<float>(gltfMaterial.alphaCutoff));
        materialInstance->setDoubleSidedOverride(gltfMaterial.doubleSided);

        return materialInstance;
    }

    TRef<StaticMesh> GLTFLoader::importStaticMesh(
        const tinygltf::Model& model,
        const tinygltf::Mesh& gltfMesh,
        std::span<const TRef<MaterialInstance>> importedMaterials,
        bool importMaterials
    )
    {
        std::vector<GLTFPrimitiveData> primitives;
        primitives.reserve(gltfMesh.primitives.size());

        bool needsUint32Index = false;
        bool anyHasColor = false;
        uint32_t totalVertexCount = 0;
        uint32_t totalIndexCount = 0;

        for (const tinygltf::Primitive& gltfPrimitive : gltfMesh.primitives)
        {
            std::optional<GLTFPrimitiveData> primitive = readGLTFPrimitive(model, gltfPrimitive, false);
            if (!primitive)
                continue;

            needsUint32Index |= primitive->needsUint32Index;
            if (!primitive->colors.empty())
                anyHasColor = true;

            totalVertexCount += static_cast<uint32_t>(primitive->positions.size());
            totalIndexCount += static_cast<uint32_t>(primitive->indices.size());
            primitives.push_back(std::move(*primitive));
        }

        if (primitives.empty())
            return nullptr;

        // 只要有任意一个 primitive 带 COLOR_0, 整个 LOD 就统一带上 Color 这个属性流——
        // Mesh::LOD 的顶点属性是整个 LOD 共用一份, 没法按 primitive 各自不同
        if (anyHasColor)
        {
            for (auto& primitive : primitives)
            {
                if (primitive.colors.size() != primitive.positions.size())
                    primitive.colors.assign(primitive.positions.size(), float4(1, 1, 1, 1));
            }
        }

        std::vector<VertexAttribute> vertexAttributes = {
            { VertexAttributeType::Position, sizeof(float3) },
            { VertexAttributeType::Normal, sizeof(float3) },
            { VertexAttributeType::Tangent, sizeof(float4) },
        };
        if (anyHasColor)
            vertexAttributes.push_back({ VertexAttributeType::Color, sizeof(float4) });
        vertexAttributes.push_back({ VertexAttributeType::TexCoord, sizeof(float2) });

        size_t vertexStride = sizeof(float3) + sizeof(float3) + sizeof(float4) + sizeof(float2);
        if (anyHasColor)
            vertexStride += sizeof(float4);

        std::vector<uint8_t> vertexData(vertexStride * totalVertexCount);
        float3* positionsOut = reinterpret_cast<float3*>(vertexData.data());
        float3* normalsOut = reinterpret_cast<float3*>(vertexData.data() + sizeof(float3) * totalVertexCount);
        float4* tangentsOut = reinterpret_cast<float4*>(vertexData.data() + sizeof(float3) * 2 * totalVertexCount);
        uint8_t* afterTangents = vertexData.data() + (sizeof(float3) * 2 + sizeof(float4)) * totalVertexCount;
        float4* colorsOut = anyHasColor ? reinterpret_cast<float4*>(afterTangents) : nullptr;
        float2* texCoordsOut = reinterpret_cast<float2*>(afterTangents + (anyHasColor ? sizeof(float4) * totalVertexCount : 0));

        IndexType indexType = needsUint32Index ? IndexType::Uint32 : IndexType::Uint16;
        std::vector<uint8_t> indexData(totalIndexCount * (indexType == IndexType::Uint32 ? sizeof(uint32_t) : sizeof(uint16_t)));

        struct SubmeshInfo
        {
            uint32_t vertexOffset;
            uint32_t vertexCount;
            uint32_t indexOffset;
            uint32_t indexCount;
            int materialIndex;
        };
        std::vector<SubmeshInfo> submeshInfos;
        submeshInfos.reserve(primitives.size());

        uint32_t vertexCursor = 0;
        uint32_t indexCursor = 0;
        for (auto& primitive : primitives)
        {
            uint32_t vertexCount = static_cast<uint32_t>(primitive.positions.size());
            uint32_t indexCount = static_cast<uint32_t>(primitive.indices.size());

            std::memcpy(positionsOut + vertexCursor, primitive.positions.data(), vertexCount * sizeof(float3));
            std::memcpy(normalsOut + vertexCursor, primitive.normals.data(), vertexCount * sizeof(float3));
            std::memcpy(tangentsOut + vertexCursor, primitive.tangents.data(), vertexCount * sizeof(float4));
            std::memcpy(texCoordsOut + vertexCursor, primitive.texCoord0s.data(), vertexCount * sizeof(float2));
            if (anyHasColor)
                std::memcpy(colorsOut + vertexCursor, primitive.colors.data(), vertexCount * sizeof(float4));

            if (indexType == IndexType::Uint32)
            {
                std::memcpy(reinterpret_cast<uint32_t*>(indexData.data()) + indexCursor, primitive.indices.data(), indexCount * sizeof(uint32_t));
            }
            else
            {
                uint16_t* dst = reinterpret_cast<uint16_t*>(indexData.data()) + indexCursor;
                for (uint32_t i = 0; i < indexCount; ++i)
                    dst[i] = static_cast<uint16_t>(primitive.indices[i]);
            }

            submeshInfos.push_back({ vertexCursor, vertexCount, indexCursor, indexCount, primitive.materialIndex });

            vertexCursor += vertexCount;
            indexCursor += indexCount;
        }

        TRef<StaticMesh> mesh = createObject<StaticMesh>(nullptr);
        uint32_t lodLevel = mesh->addLOD();
        mesh->setLODData(lodLevel, std::move(vertexData), std::move(vertexAttributes), std::move(indexData), indexType, 0.01f);

        // 同一个 glTF materialIndex 在本 Mesh 内复用同一个材质槽位——槽位分组只看 glTF 自己
        // 的材质索引, 不依赖传给 addMaterialSlot() 的指针是否相同(Mesh::addMaterialSlot()
        // 不做基于指针的去重, 见 Mesh.h 的说明), 这样"改一次某个 glTF 材质, 所有引用它的
        // 子网格一起变"的分组关系才不会被打散。
        std::unordered_map<int, uint32_t> materialSlotByGltfIndex;

        for (const auto& info : submeshInfos)
        {
            auto slotIt = materialSlotByGltfIndex.find(info.materialIndex);
            uint32_t materialSlot;
            if (slotIt != materialSlotByGltfIndex.end())
            {
                materialSlot = slotIt->second;
            }
            else
            {
                Material* material;
                if (importMaterials && info.materialIndex >= 0 && static_cast<size_t>(info.materialIndex) < importedMaterials.size())
                {
                    material = importedMaterials[info.materialIndex].get();
                }
                else if (!importMaterials)
                {
                    // 直接引用 WorldGridMaterial 这份落盘的引擎内置资产本身, 序列化成普通的
                    // 外部资产引用(GUID)
                    material = getWorldGridTemplate();
                }
                else
                {
                    // primitive 本来就没指定材质索引(或索引越界): 导入材质时兜底到 GLTFMaterial
                    // 模板自己的默认参数(纯白 + metallic=1, 跟 glTF 规范定义的默认材质一致)
                    material = getGltfPbrTemplate();
                }

                materialSlot = mesh->addMaterialSlot(material);
                materialSlotByGltfIndex.emplace(info.materialIndex, materialSlot);
            }

            mesh->addSubmesh(lodLevel, info.vertexOffset, info.vertexCount, info.indexOffset, info.indexCount, materialSlot);
        }

        return mesh;
    }

    TRef<SkeletalMesh> GLTFLoader::importSkeletalMesh(
        const tinygltf::Model& model,
        const tinygltf::Mesh& gltfMesh,
        Skeleton* skeleton,
        std::span<const uint32_t> jointIndexRemap,
        std::span<const TRef<MaterialInstance>> importedMaterials,
        bool importMaterials
    )
    {
        std::vector<GLTFPrimitiveData> primitives;
        primitives.reserve(gltfMesh.primitives.size());

        bool needsUint32Index = false;
        bool anyHasColor = false;
        uint32_t totalVertexCount = 0;
        uint32_t totalIndexCount = 0;

        for (const tinygltf::Primitive& gltfPrimitive : gltfMesh.primitives)
        {
            std::optional<GLTFPrimitiveData> primitive = readGLTFPrimitive(model, gltfPrimitive, true);
            if (!primitive)
                continue;

            size_t vertexCount = primitive->positions.size();
            // 缺 JOINTS_0/WEIGHTS_0(或跟顶点数对不上)的顶点兜底成"完全绑定第 0 号骨骼、
            // 权重 1"——跟 VertexLayout.cpp 里非蒙皮场景的着色器 getter 默认值是同一个约定
            if (primitive->joints.empty())
                primitive->joints.assign(vertexCount, uint4(0, 0, 0, 0));
            if (primitive->weights.empty())
                primitive->weights.assign(vertexCount, float4(1, 0, 0, 0));

            // JOINTS_0 里的索引是 skin.joints 数组下标, 要 remap 成 Skeleton 里的骨骼下标(见
            // importSkeleton() 的说明)
            for (uint4& joint : primitive->joints)
            {
                for (int c = 0; c < 4; ++c)
                {
                    uint32_t original = joint[c];
                    joint[c] = original < jointIndexRemap.size() ? jointIndexRemap[original] : 0;
                }
            }

            needsUint32Index |= primitive->needsUint32Index;
            if (!primitive->colors.empty())
                anyHasColor = true;

            totalVertexCount += static_cast<uint32_t>(vertexCount);
            totalIndexCount += static_cast<uint32_t>(primitive->indices.size());
            primitives.push_back(std::move(*primitive));
        }

        if (primitives.empty())
            return nullptr;

        // 只要有任意一个 primitive 带 COLOR_0, 整个 LOD 就统一带上 Color 这个属性流——
        // Mesh::LOD 的顶点属性是整个 LOD 共用一份, 没法按 primitive 各自不同
        if (anyHasColor)
        {
            for (auto& primitive : primitives)
            {
                if (primitive.colors.size() != primitive.positions.size())
                    primitive.colors.assign(primitive.positions.size(), float4(1, 1, 1, 1));
            }
        }

        std::vector<VertexAttribute> vertexAttributes = {
            { VertexAttributeType::Position, sizeof(float3) },
            { VertexAttributeType::Normal, sizeof(float3) },
            { VertexAttributeType::Tangent, sizeof(float4) },
        };
        if (anyHasColor)
            vertexAttributes.push_back({ VertexAttributeType::Color, sizeof(float4) });
        vertexAttributes.push_back({ VertexAttributeType::TexCoord, sizeof(float2) });
        vertexAttributes.push_back({ VertexAttributeType::Bone, sizeof(uint4) });
        vertexAttributes.push_back({ VertexAttributeType::Weight, sizeof(float4) });

        size_t vertexStride = sizeof(float3) + sizeof(float3) + sizeof(float4) + sizeof(float2) + sizeof(uint4) + sizeof(float4);
        if (anyHasColor)
            vertexStride += sizeof(float4);

        std::vector<uint8_t> vertexData(vertexStride * totalVertexCount);
        float3* positionsOut = reinterpret_cast<float3*>(vertexData.data());
        float3* normalsOut = reinterpret_cast<float3*>(vertexData.data() + sizeof(float3) * totalVertexCount);
        float4* tangentsOut = reinterpret_cast<float4*>(vertexData.data() + sizeof(float3) * 2 * totalVertexCount);
        uint8_t* afterTangents = vertexData.data() + (sizeof(float3) * 2 + sizeof(float4)) * totalVertexCount;
        float4* colorsOut = anyHasColor ? reinterpret_cast<float4*>(afterTangents) : nullptr;
        uint8_t* afterColors = afterTangents + (anyHasColor ? sizeof(float4) * totalVertexCount : 0);
        float2* texCoordsOut = reinterpret_cast<float2*>(afterColors);
        uint8_t* afterTexCoords = afterColors + sizeof(float2) * totalVertexCount;
        uint4* jointsOut = reinterpret_cast<uint4*>(afterTexCoords);
        float4* weightsOut = reinterpret_cast<float4*>(afterTexCoords + sizeof(uint4) * totalVertexCount);

        IndexType indexType = needsUint32Index ? IndexType::Uint32 : IndexType::Uint16;
        std::vector<uint8_t> indexData(totalIndexCount * (indexType == IndexType::Uint32 ? sizeof(uint32_t) : sizeof(uint16_t)));

        struct SubmeshInfo
        {
            uint32_t vertexOffset;
            uint32_t vertexCount;
            uint32_t indexOffset;
            uint32_t indexCount;
            int materialIndex;
        };
        std::vector<SubmeshInfo> submeshInfos;
        submeshInfos.reserve(primitives.size());

        uint32_t vertexCursor = 0;
        uint32_t indexCursor = 0;
        for (auto& primitive : primitives)
        {
            uint32_t vertexCount = static_cast<uint32_t>(primitive.positions.size());
            uint32_t indexCount = static_cast<uint32_t>(primitive.indices.size());

            std::memcpy(positionsOut + vertexCursor, primitive.positions.data(), vertexCount * sizeof(float3));
            std::memcpy(normalsOut + vertexCursor, primitive.normals.data(), vertexCount * sizeof(float3));
            std::memcpy(tangentsOut + vertexCursor, primitive.tangents.data(), vertexCount * sizeof(float4));
            std::memcpy(texCoordsOut + vertexCursor, primitive.texCoord0s.data(), vertexCount * sizeof(float2));
            if (anyHasColor)
                std::memcpy(colorsOut + vertexCursor, primitive.colors.data(), vertexCount * sizeof(float4));
            std::memcpy(jointsOut + vertexCursor, primitive.joints.data(), vertexCount * sizeof(uint4));
            std::memcpy(weightsOut + vertexCursor, primitive.weights.data(), vertexCount * sizeof(float4));

            if (indexType == IndexType::Uint32)
            {
                std::memcpy(reinterpret_cast<uint32_t*>(indexData.data()) + indexCursor, primitive.indices.data(), indexCount * sizeof(uint32_t));
            }
            else
            {
                uint16_t* dst = reinterpret_cast<uint16_t*>(indexData.data()) + indexCursor;
                for (uint32_t i = 0; i < indexCount; ++i)
                    dst[i] = static_cast<uint16_t>(primitive.indices[i]);
            }

            submeshInfos.push_back({ vertexCursor, vertexCount, indexCursor, indexCount, primitive.materialIndex });

            vertexCursor += vertexCount;
            indexCursor += indexCount;
        }

        TRef<SkeletalMesh> mesh = createObject<SkeletalMesh>(nullptr);
        mesh->setSkeleton(skeleton);
        uint32_t lodLevel = mesh->addLOD();
        mesh->setLODData(lodLevel, std::move(vertexData), std::move(vertexAttributes), std::move(indexData), indexType, 0.01f);

        // 材质槽位分组逻辑跟 importStaticMesh() 完全一样(见那边的注释), Mesh::addMaterialSlot()/
        // addSubmesh() 是 SkeletalMesh 从 Mesh 继承来的同一套接口
        std::unordered_map<int, uint32_t> materialSlotByGltfIndex;

        for (const auto& info : submeshInfos)
        {
            auto slotIt = materialSlotByGltfIndex.find(info.materialIndex);
            uint32_t materialSlot;
            if (slotIt != materialSlotByGltfIndex.end())
            {
                materialSlot = slotIt->second;
            }
            else
            {
                Material* material;
                if (importMaterials && info.materialIndex >= 0 && static_cast<size_t>(info.materialIndex) < importedMaterials.size())
                {
                    material = importedMaterials[info.materialIndex].get();
                }
                else if (!importMaterials)
                {
                    material = getWorldGridTemplate();
                }
                else
                {
                    material = getGltfPbrTemplate();
                }

                materialSlot = mesh->addMaterialSlot(material);
                materialSlotByGltfIndex.emplace(info.materialIndex, materialSlot);
            }

            mesh->addSubmesh(lodLevel, info.vertexOffset, info.vertexCount, info.indexOffset, info.indexCount, materialSlot);
        }

        return mesh;
    }

    namespace
    {
        // 沿 nodeParent 往上一直走到"父节点本身也是某个 joint"或者"没有父节点"为止——中间
        // 经过的这些非 joint 祖先节点(典型的比如 Blender 导出时场景最上层的空物体)的局部
        // 变换, glTF 里合法地可以携带缩放(常见于建模时按错误单位工作、靠这层节点做换算),
        // 但 Skeleton 只关心 joint 集合本身的父子链——网格的位置是通过完整的 node 层级
        // (Entity/Transform)正常算出来的, 天然会经过这层节点; 骨架的 bind pose 如果只看
        // joint 自己的局部 TRS、找不到父 joint 就当成没有更上层变换, 这层变换就丢了。返回值
        // 是这段祖先链的局部变换累乘(离场景根最远的祖先在最外层), 没有非 joint 祖先时是
        // 单位矩阵。
        float4x4 accumulateNonJointAncestorMatrix(
            const tinygltf::Model& model,
            const std::unordered_map<int, int>& nodeParent,
            const std::unordered_map<int, uint32_t>& originalJointIndexByNode,
            int jointNodeIndex)
        {
            std::vector<int> ancestorChain; // 从最近的非 joint 父开始, 往上一直到场景根
            int current = jointNodeIndex;
            while (true)
            {
                auto parentIt = nodeParent.find(current);
                if (parentIt == nodeParent.end())
                    break;
                int parentNode = parentIt->second;
                if (originalJointIndexByNode.find(parentNode) != originalJointIndexByNode.end())
                    break; // 父节点本身是 joint, 交给 joint 层级自己的父子链处理
                ancestorChain.push_back(parentNode);
                current = parentNode;
            }

            float4x4 result = glm::identity<float4x4>();
            // ancestorChain 是从近到远, 组合矩阵要从远到近乘(离场景根最远的祖先在最外层)
            for (auto it = ancestorChain.rbegin(); it != ancestorChain.rend(); ++it)
            {
                LocalTRS trs = readNodeLocalTRS(model.nodes[*it]);
                float4x4 localMatrix = glm::translate(glm::identity<float4x4>(), gltfToEngineAxis(trs.position)) *
                    glm::mat4_cast(gltfToEngineAxis(trs.rotation)) *
                    glm::scale(glm::identity<float4x4>(), float3(trs.scale.x, trs.scale.z, trs.scale.y));
                result = result * localMatrix;
            }
            return result;
        }
    }

    TRef<Skeleton> GLTFLoader::importSkeleton(
        const tinygltf::Model& model,
        const tinygltf::Skin& gltfSkin,
        std::vector<uint32_t>& outJointIndexRemap,
        std::unordered_map<HString, float4x4>& outRootAncestorMatrices
    )
    {
        outJointIndexRemap.assign(gltfSkin.joints.size(), 0);
        if (gltfSkin.joints.empty())
            return nullptr;

        // node 下标 -> 父 node 下标, 整个 model 扫一遍建出来(children 只记正向, 没有反向指针)
        std::unordered_map<int, int> nodeParent;
        for (size_t nodeIndex = 0; nodeIndex < model.nodes.size(); ++nodeIndex)
        {
            for (int childIndex : model.nodes[nodeIndex].children)
                nodeParent[childIndex] = static_cast<int>(nodeIndex);
        }

        // joint 的 node 下标 -> 它在 skin.joints 里的原始下标(JOINTS_0 顶点属性引用的就是这个)
        std::unordered_map<int, uint32_t> originalJointIndexByNode;
        for (size_t i = 0; i < gltfSkin.joints.size(); ++i)
            originalJointIndexByNode.emplace(gltfSkin.joints[i], static_cast<uint32_t>(i));

        // 按父 joint 分组每个 joint 的直接子 joint, 父不在 joint 集合里就当根处理——
        // skin.joints 数组本身不保证父在前子在后, 下面再做一次 BFS 重新分配下标
        std::vector<std::vector<int>> childJointNodesOf(gltfSkin.joints.size());
        std::vector<int> rootJointNodes;
        for (int jointNodeIndex : gltfSkin.joints)
        {
            auto parentIt = nodeParent.find(jointNodeIndex);
            auto parentJointIt = parentIt != nodeParent.end() ? originalJointIndexByNode.find(parentIt->second) : originalJointIndexByNode.end();
            if (parentJointIt != originalJointIndexByNode.end())
                childJointNodesOf[parentJointIt->second].push_back(jointNodeIndex);
            else
                rootJointNodes.push_back(jointNodeIndex);
        }

        TRef<Skeleton> skeleton = createObject<Skeleton>(nullptr);

        std::vector<std::pair<int, int32_t>> pending; // (joint 的 node 下标, 父骨骼在 Skeleton 里的新下标)
        pending.reserve(gltfSkin.joints.size());
        for (int rootNodeIndex : rootJointNodes)
            pending.emplace_back(rootNodeIndex, -1);

        for (size_t cursor = 0; cursor < pending.size(); ++cursor)
        {
            auto [jointNodeIndex, parentNewIndex] = pending[cursor];
            const tinygltf::Node& node = model.nodes[jointNodeIndex];
            LocalTRS trs = readNodeLocalTRS(node);

            float3 bonePosition = gltfToEngineAxis(trs.position);
            Quaternion boneRotation = gltfToEngineAxis(trs.rotation);
            float3 boneScale = float3(trs.scale.x, trs.scale.z, trs.scale.y); // 同一个轴变换下, 缩放量只随轴换位置, 不需要跟位置/旋转一样处理符号

            // 没名字的关节 fallback 成 "Bone_" + 骨骼下标(而不是不带下标的固定 "Bone")——
            // HString 对 "前缀_数字" 这种后缀会做优化, 且能保证同一个 Skeleton 内不会因为
            // 多个 unnamed 关节撞名而让 findBoneIndex()/动画的 BoneTrack 绑定产生歧义
            HString boneName = !node.name.empty() ? HString(node.name) : HString("Bone_" + std::to_string(skeleton->getBoneCount()));

            // 根骨骼(在 joint 集合里没有父)可能上面还挂着非 joint 的祖先节点, 那层变换要
            // 烘焙进这根骨骼自己的 bind pose 局部 TRS 里, 并且额外记一份矩阵交给
            // importAnimation() 去修正这根骨骼的动画关键帧——两处都要处理, 见 GLTFLoader.h
            // 里 outRootAncestorMatrices 参数的说明
            if (parentNewIndex < 0)
            {
                float4x4 ancestorMatrix = accumulateNonJointAncestorMatrix(model, nodeParent, originalJointIndexByNode, jointNodeIndex);
                if (ancestorMatrix != glm::identity<float4x4>())
                {
                    float4x4 jointLocalMatrix = glm::translate(glm::identity<float4x4>(), bonePosition) *
                        glm::mat4_cast(boneRotation) *
                        glm::scale(glm::identity<float4x4>(), boneScale);
                    float4x4 combined = ancestorMatrix * jointLocalMatrix;

                    glm::dmat4 dCombined(combined);
                    glm::dvec3 dScale, dTranslation, dSkew;
                    glm::dvec4 dPerspective;
                    glm::dquat dRotation;
                    glm::decompose(dCombined, dScale, dRotation, dTranslation, dSkew, dPerspective);

                    bonePosition = float3(dTranslation);
                    boneRotation = Quaternion(static_cast<float>(dRotation.w), static_cast<float>(dRotation.x), static_cast<float>(dRotation.y), static_cast<float>(dRotation.z));
                    boneScale = float3(dScale);

                    outRootAncestorMatrices.emplace(boneName, ancestorMatrix);
                }
            }

            uint32_t newBoneIndex = skeleton->addBone(boneName, parentNewIndex, bonePosition, boneRotation, boneScale);

            uint32_t originalJointIndex = originalJointIndexByNode[jointNodeIndex];
            outJointIndexRemap[originalJointIndex] = newBoneIndex;

            for (int childNodeIndex : childJointNodesOf[originalJointIndex])
                pending.emplace_back(childNodeIndex, static_cast<int32_t>(newBoneIndex));
        }

        return skeleton;
    }

    TRef<AnimationClip> GLTFLoader::importAnimation(
        const tinygltf::Model& model,
        const tinygltf::Animation& gltfAnimation,
        const std::unordered_map<int, HString>& boneNameByNode,
        const std::unordered_map<HString, float4x4>& rootAncestorMatrices
    )
    {
        TRef<AnimationClip> clip = createObject<AnimationClip>(nullptr);
        // glTF 动画规范里没有循环标志这个概念, 引擎侧 Animation::mLooping 默认是 false——
        // 但美术导出的角色动画(walk/idle 之类)绝大多数场景下都是想循环播放的, 导入时直接
        // 打开, 需要单次播放的动画(比如攻击)按需在编辑器里手动关掉
        clip->setLooping(true);
        float duration = 0.0f;
        bool anyTrackWritten = false;

        for (const tinygltf::AnimationChannel& channel : gltfAnimation.channels)
        {
            // 目标 node 不是任何已导入 Skeleton 的关节——网格/相机/灯光节点本身的变换动画,
            // 或者蒙皮导入被关闭(boneNameByNode 整张表是空的), 都在这里被跳过
            auto boneIt = boneNameByNode.find(channel.target_node);
            if (boneIt == boneNameByNode.end())
                continue;
            if (channel.sampler < 0 || static_cast<size_t>(channel.sampler) >= gltfAnimation.samplers.size())
                continue;

            const tinygltf::AnimationSampler& sampler = gltfAnimation.samplers[channel.sampler];
            std::vector<float> times = readAccessorFloats(model, sampler.input, 1);
            if (times.empty())
                continue;

            // CUBICSPLINE 的 output 是每个关键帧(入切线, 值, 出切线)三元组——BoneTrack 只支持
            // 线性/Slerp 插值, 不重建切线, 直接取三元组中间的值当普通关键帧用
            bool cubicSpline = sampler.interpolation == "CUBICSPLINE";
            size_t stride = cubicSpline ? 3 : 1;

            int32_t trackIndex = clip->resolveBoneTrackIndex(boneIt->second);
            if (trackIndex < 0)
                trackIndex = static_cast<int32_t>(clip->addBoneTrack(boneIt->second));
            BoneTrack& track = clip->getBoneTrack(static_cast<uint32_t>(trackIndex));

            // 这根骨骼是不是带非 joint 祖先变换的根骨骼——见 GLTFLoader.h 里
            // rootAncestorMatrices 参数的说明, 只有查得到时下面 translation/scale 才需要修正
            auto ancestorIt = rootAncestorMatrices.find(boneIt->second);
            const float4x4* ancestorMatrix = ancestorIt != rootAncestorMatrices.end() ? &ancestorIt->second : nullptr;

            bool channelWritten = false;
            if (channel.target_path == "translation")
            {
                std::vector<float3> values = readVec3(model, sampler.output);
                if (values.size() == times.size() * stride)
                {
                    track.positionKeys.reserve(track.positionKeys.size() + times.size());
                    for (size_t k = 0; k < times.size(); ++k)
                    {
                        float3 position = gltfToEngineAxis(values[k * stride + (cubicSpline ? 1 : 0)]);
                        // 骨架 bind pose 那边已经把祖先变换烘焙进这根骨骼自己的局部 TRS 了,
                        // 动画关键帧覆盖的是同一个字段, 必须做同样的变换才能对齐, 否则跟
                        // bind pose 差出这一层没修正的变换(典型表现: 位置爆炸到几十倍)
                        if (ancestorMatrix)
                            position = float3(*ancestorMatrix * float4(position, 1.0f));
                        track.positionKeys.push_back({ times[k], position });
                    }
                    channelWritten = true;
                }
            }
            else if (channel.target_path == "rotation")
            {
                std::vector<float4> values = readVec4(model, sampler.output);
                if (values.size() == times.size() * stride)
                {
                    // 祖先矩阵如果带旋转分量, 得左乘到这根骨骼自己的旋转上——推导:
                    // ancestorMatrix * T(pos) * R(rot) * S(scale) 展开后, 旋转部分变成
                    // ancestorRotation * R(rot)(前提是祖先的缩放是等比缩放, 能跟旋转换序),
                    // 跟 translation/scale 通道的修正是同一套推导, 见函数开头 ancestorMatrix
                    // 的注释
                    Quaternion ancestorRotation(1, 0, 0, 0);
                    if (ancestorMatrix)
                    {
                        glm::dmat4 dAncestor(*ancestorMatrix);
                        glm::dvec3 dScale, dTranslation, dSkew;
                        glm::dvec4 dPerspective;
                        glm::dquat dRotation;
                        glm::decompose(dAncestor, dScale, dRotation, dTranslation, dSkew, dPerspective);
                        ancestorRotation = Quaternion(static_cast<float>(dRotation.w), static_cast<float>(dRotation.x), static_cast<float>(dRotation.y), static_cast<float>(dRotation.z));
                    }

                    track.rotationKeys.reserve(track.rotationKeys.size() + times.size());
                    for (size_t k = 0; k < times.size(); ++k)
                    {
                        const float4& v = values[k * stride + (cubicSpline ? 1 : 0)];
                        Quaternion rotation = gltfToEngineAxis(Quaternion(v.w, v.x, v.y, v.z));
                        if (ancestorMatrix)
                            rotation = ancestorRotation * rotation;
                        track.rotationKeys.push_back({ times[k], rotation });
                    }
                    channelWritten = true;
                }
            }
            else if (channel.target_path == "scale")
            {
                std::vector<float3> values = readVec3(model, sampler.output);
                if (values.size() == times.size() * stride)
                {
                    // 祖先矩阵的线性部分(3 根轴向量的长度)当缩放系数用——祖先节点目前只处理
                    // 平移+缩放的常见情况(比如 Blender 场景最上层的换算用空物体), 有旋转分量
                    // 时这里的近似会不准确, 但没有实际样本证明需要为此再做更复杂的处理
                    float3 ancestorScale(1.0f);
                    if (ancestorMatrix)
                    {
                        ancestorScale = float3(
                            glm::length(float3((*ancestorMatrix)[0])),
                            glm::length(float3((*ancestorMatrix)[1])),
                            glm::length(float3((*ancestorMatrix)[2]))
                        );
                    }

                    track.scaleKeys.reserve(track.scaleKeys.size() + times.size());
                    for (size_t k = 0; k < times.size(); ++k)
                    {
                        const float3& v = values[k * stride + (cubicSpline ? 1 : 0)];
                        float3 scale = float3(v.x, v.z, v.y);
                        if (ancestorMatrix)
                            scale *= ancestorScale;
                        track.scaleKeys.push_back({ times[k], scale });
                    }
                    channelWritten = true;
                }
            }
            // "weights"(morph target 权重)没有对应的 BoneTrack 通道, 直接跳过

            if (channelWritten)
            {
                anyTrackWritten = true;
                duration = std::max(duration, times.back());
            }
        }

        if (!anyTrackWritten)
            return nullptr;

        clip->setDuration(duration);
        return clip;
    }

    std::optional<GLTFLoadResult> GLTFLoader::load(
        const std::filesystem::path& path,
        bool importMaterials,
        bool importSkinning,
        bool importAnimations
    )
    {
        tinygltf::TinyGLTF loader;
        tinygltf::Model model;
        std::string error;
        std::string warning;

        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        bool ok = (extension == ".glb")
            ? loader.LoadBinaryFromFile(&model, &error, &warning, path.string())
            : loader.LoadASCIIFromFile(&model, &error, &warning, path.string());

        if (!warning.empty())
            LOG_WARN("[GLTFLoader] {}: {}", path.string(), warning);

        if (!ok)
        {
            LOG_ERROR("[GLTFLoader] Failed to load {}: {}", path.string(), error);
            return std::nullopt;
        }

        GLTFLoadResult result;

        std::unordered_map<uint64_t, TRef<Texture2D>> textureCache;

        // importMaterials=false 时 result.materials/materialNames 保持为空——不产出任何独立
        // 材质资产, 每个 Mesh 内部各自按需建内嵌占位材质(见 importStaticMesh() 和类注释)。
        if (importMaterials)
        {
            result.materials.reserve(model.materials.size());
            result.materialNames.reserve(model.materials.size());
            for (size_t i = 0; i < model.materials.size(); ++i)
            {
                const tinygltf::Material& gltfMaterial = model.materials[i];
                result.materials.push_back(importMaterial(model, gltfMaterial, textureCache, result.textures, result.textureNames));
                result.materialNames.push_back(!gltfMaterial.name.empty() ? gltfMaterial.name : "Material" + std::to_string(i));
            }
        }

        // glTF 的 skin 挂在 Node 上, 不挂在 Mesh 上——同一个 Mesh 理论上可能被多个 Node 引用,
        // 这里只取第一个带 skin 的引用作为"这个 Mesh 是不是骨架网格"的判定依据。importSkinning
        // 为 false 时这张表留空, 下面的循环就会让每个 mesh 都退化成普通 StaticMesh 导入,
        // 不需要另外改 importStaticMesh()/importSkeletalMesh() 的分派逻辑。
        std::unordered_map<size_t, int> skinIndexByMesh;
        if (importSkinning)
        {
            for (const tinygltf::Node& node : model.nodes)
            {
                if (node.mesh < 0 || node.skin < 0)
                    continue;
                skinIndexByMesh.try_emplace(static_cast<size_t>(node.mesh), node.skin);
            }
        }

        // 同一个 skin 只导入一次 Skeleton, 引用它的所有 SkeletalMesh 共享同一份 TRef
        std::unordered_map<int, TRef<Skeleton>> skeletonBySkinIndex;
        std::unordered_map<int, std::vector<uint32_t>> jointRemapBySkinIndex;

        // glTF node 下标 -> 已导入骨骼的名字, 给下面的动画导入用(动画 channel 按 node 下标
        // 引用目标, BoneTrack 按名字绑定, 见 importAnimation() 的参数说明)
        std::unordered_map<int, HString> boneNameByNodeIndex;

        // 按骨骼名字索引的祖先矩阵, 从各个 importSkeleton() 调用里收集——见 GLTFLoader.h 里
        // 该参数的说明, 给下面的动画导入用来修正带非 joint 祖先的根骨骼的动画关键帧
        std::unordered_map<HString, float4x4> rootAncestorMatrices;

        result.meshes.reserve(model.meshes.size());
        result.meshNames.reserve(model.meshes.size());
        result.skeletalMeshes.reserve(model.meshes.size());
        result.skeletalMeshNames.reserve(model.meshes.size());
        result.skeletons.reserve(model.skins.size());
        result.skeletonNames.reserve(model.skins.size());

        for (size_t i = 0; i < model.meshes.size(); ++i)
        {
            const tinygltf::Mesh& gltfMesh = model.meshes[i];

            auto skinIt = skinIndexByMesh.find(i);
            if (skinIt != skinIndexByMesh.end())
            {
                int skinIndex = skinIt->second;
                auto skeletonIt = skeletonBySkinIndex.find(skinIndex);
                if (skeletonIt == skeletonBySkinIndex.end())
                {
                    const tinygltf::Skin& gltfSkin = model.skins[skinIndex];
                    std::vector<uint32_t> jointRemap;
                    TRef<Skeleton> skeleton = importSkeleton(model, gltfSkin, jointRemap, rootAncestorMatrices);
                    if (skeleton)
                    {
                        result.skeletons.push_back(skeleton);
                        result.skeletonNames.push_back(!gltfSkin.name.empty() ? gltfSkin.name : "Skeleton" + std::to_string(skinIndex));
                        for (size_t j = 0; j < gltfSkin.joints.size(); ++j)
                            boneNameByNodeIndex.emplace(gltfSkin.joints[j], skeleton->getBone(jointRemap[j]).name);
                    }
                    skeletonIt = skeletonBySkinIndex.emplace(skinIndex, skeleton).first;
                    jointRemapBySkinIndex.emplace(skinIndex, std::move(jointRemap));
                }

                // skeletonIt->second 为空说明这个 skin.joints 是空的(见 importSkeleton()),
                // 这种损坏数据退化成按普通静态网格导入, 不直接丢弃整个 mesh
                if (TRef<Skeleton> skeleton = skeletonIt->second)
                {
                    if (TRef<SkeletalMesh> mesh = importSkeletalMesh(model, gltfMesh, skeleton.get(), jointRemapBySkinIndex[skinIndex], result.materials, importMaterials))
                    {
                        result.skeletalMeshes.push_back(std::move(mesh));
                        result.skeletalMeshNames.push_back(!gltfMesh.name.empty() ? gltfMesh.name : "SkeletalMesh" + std::to_string(i));
                        continue;
                    }
                }
            }

            if (TRef<StaticMesh> mesh = importStaticMesh(model, gltfMesh, result.materials, importMaterials))
            {
                result.meshes.push_back(std::move(mesh));
                result.meshNames.push_back(!gltfMesh.name.empty() ? gltfMesh.name : "Mesh" + std::to_string(i));
            }
        }

        if (importAnimations)
        {
            result.animations.reserve(model.animations.size());
            result.animationNames.reserve(model.animations.size());
            for (size_t i = 0; i < model.animations.size(); ++i)
            {
                const tinygltf::Animation& gltfAnimation = model.animations[i];
                if (TRef<AnimationClip> clip = importAnimation(model, gltfAnimation, boneNameByNodeIndex, rootAncestorMatrices))
                {
                    result.animations.push_back(std::move(clip));
                    result.animationNames.push_back(!gltfAnimation.name.empty() ? gltfAnimation.name : "Animation" + std::to_string(i));
                }
            }
        }

        return result;
    }
}
