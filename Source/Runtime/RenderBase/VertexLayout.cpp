#include "VertexLayout.h"
#include <Core/Math/Vector.h>

namespace hyp
{
    VertexLayout::VertexLayoutTable VertexLayout::sVertexLayoutTable;

    VertexLayout::VertexLayout(VertexLayoutType type, VertexAttributeMask attributeMask)
    {
        mKey = { type, attributeMask };
        auto it = sVertexLayoutTable.find(mKey);
        if (it == sVertexLayoutTable.end())
        {
            mId = static_cast<uint32_t>(sVertexLayoutTable.size());
            VertexLayoutInfo info;
            info.id = mId;
            sVertexLayoutTable.emplace(mKey, info);
        }
        else
        {
            mId = it->second.id;
        }
    }

    void VertexLayout::writeVertexLayoutBindingsAndAttributes(
        std::vector<rhi::VertexInputBinding>& bindings,
        std::vector<rhi::VertexInputAttribute>& attributes
    ) const
    {
        switch (mKey.type)
        {
        case VertexLayoutType::Skeletal_Mesh:
        case VertexLayoutType::Static_Mesh:
        {
            bindings.reserve(6);
            attributes.reserve(6);
            uint32_t binding = 0;
            if (enumHasAnyFlags(mKey.attributeMask, VertexAttributeMask::Position))
            {
                bindings.push_back({ binding, sizeof(float3), rhi::VertexInputRate::Vertex });
                attributes.push_back({ binding, binding, rhi::VertexFormat::Float32x3, 0 });
                ++binding;
            }
            if (enumHasAnyFlags(mKey.attributeMask, VertexAttributeMask::Normal))
            {
                bindings.push_back({ binding, sizeof(float3), rhi::VertexInputRate::Vertex });
                attributes.push_back({ binding, binding, rhi::VertexFormat::Float32x3, 0 });
                ++binding;
            }
            if (enumHasAnyFlags(mKey.attributeMask, VertexAttributeMask::Tangent))
            {
                bindings.push_back({ binding, sizeof(float4), rhi::VertexInputRate::Vertex });
                attributes.push_back({ binding, binding, rhi::VertexFormat::Float32x4, 0 });
                ++binding;
            }
            if (enumHasAnyFlags(mKey.attributeMask, VertexAttributeMask::Color))
            {
                bindings.push_back({ binding, sizeof(float4), rhi::VertexInputRate::Vertex });
                attributes.push_back({ binding, binding, rhi::VertexFormat::Float32x4, 0 });
                ++binding;
            }
            for (uint32_t i = 0; i < kMaxTexCoordCount; ++i)
            {
                if (enumHasAnyFlags(mKey.attributeMask, VertexAttributeMask(uint32_t(VertexAttributeMask::TexCoord0) << i)))
                {
                    bindings.push_back({ binding, sizeof(float2), rhi::VertexInputRate::Vertex });
                    attributes.push_back({ binding, binding, rhi::VertexFormat::Float32x2, 0 });
                    ++binding;
                }
            }
            if (mKey.type == VertexLayoutType::Skeletal_Mesh)
            {
                static constexpr VertexAttributeMask kBoneWeightMasks[] = {
                    VertexAttributeMask::Bone_Weight0, VertexAttributeMask::Bone_Weight1,
                };
                for (size_t i = 0; i < sizeof(kBoneWeightMasks) / sizeof(kBoneWeightMasks[0]); ++i)
                {
                    if (enumHasAnyFlags(mKey.attributeMask, kBoneWeightMasks[i]))
                    {
                        // 每个 Bone_WeightN 位对应一对流: uint4 骨骼索引紧跟着 float4 权重
                        bindings.push_back({ binding, sizeof(uint32_t) * 4, rhi::VertexInputRate::Vertex });
                        attributes.push_back({ binding, binding, rhi::VertexFormat::Uint32x4, 0 });
                        ++binding;
                        bindings.push_back({ binding, sizeof(float) * 4, rhi::VertexInputRate::Vertex });
                        attributes.push_back({ binding, binding, rhi::VertexFormat::Float32x4, 0 });
                        ++binding;
                    }
                }
            }
            break;
        }
        default:
            break;
        }
    }

    const std::string& VertexLayout::getVertexAttributeImplementation() const
    {
        auto& entry = sVertexLayoutTable[mKey];
        if (!entry.vertexAttributeImplementation.empty())
        {
            return entry.vertexAttributeImplementation;
        }
        entry.vertexAttributeImplementation = generateVertexAttributeImplementation();
        return entry.vertexAttributeImplementation;
    }

    std::string VertexLayout::generateVertexAttributeImplementation() const
    {
        const bool isSkeletalMesh = mKey.type == VertexLayoutType::Skeletal_Mesh;
        const bool isQuad = mKey.type == VertexLayoutType::Quad;

        std::string implementation;
        auto defineFlag = [&implementation](const std::string& name, bool enabled)
        {
            implementation += "#define " + name + (enabled ? " 1\n" : " 0\n");
        };
        auto hasAttribute = [this](VertexAttributeMask attribute)
        {
            return enumHasAllFlags(mKey.attributeMask, attribute);
        };

        defineFlag("VERTEX_PULLING", isQuad);
        defineFlag("VERTEX_HAS_POSITION", hasAttribute(VertexAttributeMask::Position));
        defineFlag("VERTEX_HAS_NORMAL", hasAttribute(VertexAttributeMask::Normal));
        defineFlag("VERTEX_HAS_TANGENT", hasAttribute(VertexAttributeMask::Tangent));
        defineFlag("VERTEX_HAS_COLOR", hasAttribute(VertexAttributeMask::Color));

        static constexpr VertexAttributeMask kTexCoordMasks[] = {
            VertexAttributeMask::TexCoord0, VertexAttributeMask::TexCoord1,
            VertexAttributeMask::TexCoord2, VertexAttributeMask::TexCoord3,
        };
        for (size_t i = 0; i < sizeof(kTexCoordMasks) / sizeof(kTexCoordMasks[0]); ++i)
        {
            defineFlag("VERTEX_HAS_TEXCOORD" + std::to_string(i), hasAttribute(kTexCoordMasks[i]));
        }

        static constexpr VertexAttributeMask kBoneWeightMasks[] = {
            VertexAttributeMask::Bone_Weight0, VertexAttributeMask::Bone_Weight1,
        };
        for (size_t i = 0; i < sizeof(kBoneWeightMasks) / sizeof(kBoneWeightMasks[0]); ++i)
        {
            defineFlag("VERTEX_HAS_BONE_WEIGHT" + std::to_string(i), isSkeletalMesh && hasAttribute(kBoneWeightMasks[i]));
        }

        if (isQuad)
            implementation += "#include <VertexLayout/Quad.slang>\n";
        else if (isSkeletalMesh)
            implementation += "#include <VertexLayout/SkeletalMesh.slang>\n";
        else
            implementation += "#include <VertexLayout/StaticMesh.slang>\n";

        return implementation;
    }

}

