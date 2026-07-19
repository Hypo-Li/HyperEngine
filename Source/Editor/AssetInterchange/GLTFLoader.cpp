#include "GLTFLoader.h"

// AssetInterchange 是唯一链接 tinygltf 的模块, 实现宏在这一份 .cpp 里定义一次——
// 多个 .cpp 都定义会导致重复符号链接错误
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include <tiny_gltf.h>

#include <Core/Common/LogSystem.h>
#include <Core/Math/Vector.h>
#include <Core/Object/Object.h>
#include <Engine/Material/MaterialInstance.h>
#include <Engine/Material/MaterialTemplate.h>
#include <Engine/Mesh/Mesh.h>
#include <Engine/Texture/SampleState.h>
#include <Engine/Texture/Texture2D.h>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace hyp::editor
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

                const float3& p0 = positions[i0];
                const float3& p1 = positions[i1];
                const float3& p2 = positions[i2];

                const float2& uv0 = texCoord0s[i0];
                const float2& uv1 = texCoord0s[i1];
                const float2& uv2 = texCoord0s[i2];

                float3 edge1 = p1 - p0;
                float3 edge2 = p2 - p0;
                float2 deltaUV1 = uv1 - uv0;
                float2 deltaUV2 = uv2 - uv0;

                float denom = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;
                if (std::abs(denom) < 1e-8f)
                    continue;
                float f = 1.0f / denom;

                float3 tangent(
                    f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x),
                    f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y),
                    f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z)
                );
                float3 bitangent(
                    f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x),
                    f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y),
                    f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z)
                );

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

    Texture2D* GLTFLoader::getOrCreateDefaultWhiteTexture()
    {
        if (mDefaultWhiteTexture)
            return mDefaultWhiteTexture.get();

        TRef<Texture2D> texture = createObject<Texture2D>(nullptr);
        texture->initialize(1, 1, PixelFormat::RGBA8_UNORM, false, CompressionPreset::None, false);
        std::vector<uint8_t> pixels = { 255, 255, 255, 255 };
        texture->setSourcePixels(std::move(pixels));

        mDefaultWhiteTexture = texture;
        return mDefaultWhiteTexture.get();
    }

    Texture2D* GLTFLoader::getOrCreateDefaultFlatNormalTexture()
    {
        if (mDefaultFlatNormalTexture)
            return mDefaultFlatNormalTexture.get();

        // 1x1 的占位贴图不适合套块压缩格式(BC 系列按 4x4 block 压缩, 1x1 会被硬 pad 成一个
        // block, 纯属浪费), 和默认白贴图一样用 None——shader 侧只读 .rg 再重建 Z, 不管底层是
        // 不是 BC5 存储都一样正确解码
        TRef<Texture2D> texture = createObject<Texture2D>(nullptr);
        texture->initialize(1, 1, PixelFormat::RGBA8_UNORM, false, CompressionPreset::None, false);
        std::vector<uint8_t> pixels = { 128, 128, 255, 255 };
        texture->setSourcePixels(std::move(pixels));

        mDefaultFlatNormalTexture = texture;
        return mDefaultFlatNormalTexture.get();
    }

    MaterialTemplate* GLTFLoader::getOrCreateGltfPbrTemplate()
    {
        if (mGltfPbrMaterialTemplate)
            return mGltfPbrMaterialTemplate.get();

        Texture2D* whiteTexture = getOrCreateDefaultWhiteTexture();
        Texture2D* flatNormalTexture = getOrCreateDefaultFlatNormalTexture();

        TRef<MaterialTemplate> materialTemplate = createObject<MaterialTemplate>(nullptr);
        materialTemplate->setBlendMode(BlendMode::Opaque);
        materialTemplate->setShadingModel(ShadingModel::Standard);

        // 参数命名和取值都对齐 glTF 2.0 的 metallic-roughness 材质模型;没有贴图的槽位默认
        // 绑一张 1x1 占位贴图(白色或平法线), 这样"没贴图"时 factor * 占位贴图采样值 正好
        // 退化成纯 factor, evaluateMaterial 不需要为"有没有贴图"写分支——和 Main.cpp 里
        // 那份手写材质用的是同一个约定
        materialTemplate->addParameter("BaseColorFactor", float4(1, 1, 1, 1));
        materialTemplate->addParameter("BaseColor", whiteTexture);
        materialTemplate->addParameter("MetallicFactor", 1.0f);
        materialTemplate->addParameter("RoughnessFactor", 1.0f);
        materialTemplate->addParameter("MetallicRoughness", whiteTexture);
        materialTemplate->addParameter("Normal", flatNormalTexture);
        materialTemplate->addParameter("Occlusion", whiteTexture);
        materialTemplate->addParameter("EmissiveFactor", float3(0, 0, 0));
        materialTemplate->addParameter("Emissive", whiteTexture);

        // glTF 规范: metallic 采 B 通道、roughness 采 G 通道; occlusion 采 R 通道; 顶点色
        // (mi.color, 没有 COLOR_0 时默认 1)按规范也要乘进 baseColor。
        // 法线贴图这份模板用 CompressionPreset::Normal_Map(见 getOrImportTexture), 对应
        // Texture::convertToRHITextureFormat() 里选的是 BC5——只存 R/G 两个通道, 采样出来的
        // .b/.a 是格式固定补的值(B=0/A=1), 不是纹理数据, 所以只能读 .rg 解码 X/Y, Z 用
        // sqrt(1-x²-y²) 重建(标准切线空间法线贴图约定, Z 分量恒为正), 不能直接读 .rgb
        materialTemplate->setMaterialSource(R"(
void evaluateMaterial(in MaterialInput mi, inout MaterialOutput mo)
{
    float4 baseColor = BaseColorFactor * BaseColorTexture.Sample(BaseColorSampler, mi.texCoords[0]) * mi.color;
    float4 metallicRoughness = MetallicRoughnessTexture.Sample(MetallicRoughnessSampler, mi.texCoords[0]);
    float3 emissive = EmissiveFactor * EmissiveTexture.Sample(EmissiveSampler, mi.texCoords[0]).rgb;
    float occlusion = OcclusionTexture.Sample(OcclusionSampler, mi.texCoords[0]).r;
    float2 normalXY = NormalTexture.Sample(NormalSampler, mi.texCoords[0]).rg * 2.0 - 1.0;
    float normalZ = sqrt(saturate(1.0 - dot(normalXY, normalXY)));
    float3 normalTS = float3(normalXY, normalZ);

    mo.baseColor = baseColor.rgb;
    mo.opacity = baseColor.a;
    mo.metallic = MetallicFactor * metallicRoughness.b;
    mo.roughness = RoughnessFactor * metallicRoughness.g;
    mo.emissive = emissive;
    mo.occlusion = occlusion;
    mo.normal = normalTS;
}
)");

        mGltfPbrMaterialTemplate = materialTemplate;
        return mGltfPbrMaterialTemplate.get();
    }

    TRef<Texture2D> GLTFLoader::getOrImportTexture(
        const tinygltf::Model& model,
        int gltfTextureIndex,
        bool isSRGB,
        CompressionPreset preset,
        std::unordered_map<uint64_t, TRef<Texture2D>>& textureCache,
        std::vector<TRef<Texture2D>>& outTextures
    )
    {
        if (gltfTextureIndex < 0 || static_cast<size_t>(gltfTextureIndex) >= model.textures.size())
            return nullptr;

        const tinygltf::Texture& gltfTexture = model.textures[gltfTextureIndex];
        if (gltfTexture.source < 0 || static_cast<size_t>(gltfTexture.source) >= model.images.size())
            return nullptr;

        uint64_t cacheKey = (static_cast<uint64_t>(gltfTexture.source) << 1) | (isSRGB ? 1u : 0u);
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
        texture->initialize(static_cast<uint32_t>(width), static_cast<uint32_t>(height), PixelFormat::RGBA8_UNORM, isSRGB, preset, true);
        texture->setSourcePixels(std::move(pixels));

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
        textureCache.emplace(cacheKey, texture);
        return texture;
    }

    TRef<MaterialInstance> GLTFLoader::importMaterial(
        const tinygltf::Model& model,
        const tinygltf::Material& gltfMaterial,
        std::unordered_map<uint64_t, TRef<Texture2D>>& textureCache,
        std::vector<TRef<Texture2D>>& outTextures
    )
    {
        MaterialTemplate* materialTemplate = getOrCreateGltfPbrTemplate();

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
        if (TRef<Texture2D> texture = getOrImportTexture(model, pbr.baseColorTexture.index, true, CompressionPreset::Default, textureCache, outTextures))
            materialInstance->setParameterOverriddenValue("BaseColor", texture.get());

        if (TRef<Texture2D> texture = getOrImportTexture(model, pbr.metallicRoughnessTexture.index, false, CompressionPreset::Default, textureCache, outTextures))
            materialInstance->setParameterOverriddenValue("MetallicRoughness", texture.get());

        if (TRef<Texture2D> texture = getOrImportTexture(model, gltfMaterial.normalTexture.index, false, CompressionPreset::Normal_Map, textureCache, outTextures))
            materialInstance->setParameterOverriddenValue("Normal", texture.get());

        if (TRef<Texture2D> texture = getOrImportTexture(model, gltfMaterial.occlusionTexture.index, false, CompressionPreset::Default, textureCache, outTextures))
            materialInstance->setParameterOverriddenValue("Occlusion", texture.get());

        if (TRef<Texture2D> texture = getOrImportTexture(model, gltfMaterial.emissiveTexture.index, true, CompressionPreset::Default, textureCache, outTextures))
            materialInstance->setParameterOverriddenValue("Emissive", texture.get());

        BlendMode blendMode = convertAlphaMode(gltfMaterial.alphaMode);
        materialInstance->setBlendModeOverride(blendMode);
        if (blendMode == BlendMode::Masked)
            materialInstance->setAlphaCutoffOverride(static_cast<float>(gltfMaterial.alphaCutoff));
        materialInstance->setDoubleSidedOverride(gltfMaterial.doubleSided);

        return materialInstance;
    }

    TRef<Mesh> GLTFLoader::importMesh(
        const tinygltf::Model& model,
        const tinygltf::Mesh& gltfMesh,
        std::span<const TRef<MaterialInstance>> importedMaterials
    )
    {
        struct PrimitiveData
        {
            std::vector<float3> positions;
            std::vector<float3> normals;
            std::vector<float4> tangents;
            std::vector<float2> texCoord0s;
            std::vector<float4> colors; // 为空表示这个 primitive 没有 COLOR_0
            std::vector<uint32_t> indices;
            int materialIndex = -1;
        };

        std::vector<PrimitiveData> primitives;
        primitives.reserve(gltfMesh.primitives.size());

        bool needsUint32Index = false;
        bool anyHasColor = false;
        uint32_t totalVertexCount = 0;
        uint32_t totalIndexCount = 0;

        for (const tinygltf::Primitive& gltfPrimitive : gltfMesh.primitives)
        {
            // 只支持三角形图元, 点/线拓扑先跳过(第一版范围外)
            if (gltfPrimitive.mode != TINYGLTF_MODE_TRIANGLES)
                continue;

            auto positionIt = gltfPrimitive.attributes.find("POSITION");
            if (positionIt == gltfPrimitive.attributes.end() || gltfPrimitive.indices < 0)
                continue;

            PrimitiveData primitive;
            primitive.positions = readVec3(model, positionIt->second);
            if (primitive.positions.empty())
                continue;
            size_t vertexCount = primitive.positions.size();

            auto normalIt = gltfPrimitive.attributes.find("NORMAL");
            primitive.normals = normalIt != gltfPrimitive.attributes.end()
                ? readVec3(model, normalIt->second)
                : std::vector<float3>(vertexCount, float3(0, 0, 1));

            auto tangentIt = gltfPrimitive.attributes.find("TANGENT");
            bool hasTangent = tangentIt != gltfPrimitive.attributes.end();
            primitive.tangents = hasTangent
                ? readVec4(model, tangentIt->second)
                : std::vector<float4>(vertexCount, float4(1, 0, 0, 1));

            auto texCoordIt = gltfPrimitive.attributes.find("TEXCOORD_0");
            primitive.texCoord0s = texCoordIt != gltfPrimitive.attributes.end()
                ? readVec2(model, texCoordIt->second)
                : std::vector<float2>(vertexCount, float2(0, 0));

            auto colorIt = gltfPrimitive.attributes.find("COLOR_0");
            if (colorIt != gltfPrimitive.attributes.end())
            {
                primitive.colors = readColor(model, colorIt->second);
                if (!primitive.colors.empty())
                    anyHasColor = true;
            }

            bool primitiveNeedsUint32 = false;
            primitive.indices = readIndices(model, gltfPrimitive.indices, primitiveNeedsUint32);
            if (primitive.indices.empty())
                continue;
            needsUint32Index |= primitiveNeedsUint32;

            if (!hasTangent && primitive.normals.size() == vertexCount && primitive.texCoord0s.size() == vertexCount)
            {
                calculateTangents(primitive.positions, primitive.normals, primitive.texCoord0s, primitive.indices, primitive.tangents);
            }

            primitive.materialIndex = gltfPrimitive.material;

            totalVertexCount += static_cast<uint32_t>(vertexCount);
            totalIndexCount += static_cast<uint32_t>(primitive.indices.size());
            primitives.push_back(std::move(primitive));
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

        TRef<Mesh> mesh = createObject<Mesh>(nullptr);
        uint32_t lodLevel = mesh->addLOD();
        mesh->setLODData(lodLevel, std::move(vertexData), std::move(vertexAttributes), std::move(indexData), indexType, 0.01f);

        for (const auto& info : submeshInfos)
        {
            Material* material = nullptr;
            if (info.materialIndex >= 0 && static_cast<size_t>(info.materialIndex) < importedMaterials.size())
                material = importedMaterials[info.materialIndex].get();
            else
                material = getOrCreateGltfPbrTemplate(); // 没指定材质的 primitive 用默认模板兜底

            mesh->addSubmesh(lodLevel, info.vertexOffset, info.vertexCount, info.indexOffset, info.indexCount, material);
        }

        return mesh;
    }

    std::optional<GLTFLoadResult> GLTFLoader::load(const std::filesystem::path& path)
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

        result.materials.reserve(model.materials.size());
        for (const tinygltf::Material& gltfMaterial : model.materials)
        {
            result.materials.push_back(importMaterial(model, gltfMaterial, textureCache, result.textures));
        }

        result.meshes.reserve(model.meshes.size());
        for (const tinygltf::Mesh& gltfMesh : model.meshes)
        {
            if (TRef<Mesh> mesh = importMesh(model, gltfMesh, result.materials))
                result.meshes.push_back(std::move(mesh));
        }

        return result;
    }
}
