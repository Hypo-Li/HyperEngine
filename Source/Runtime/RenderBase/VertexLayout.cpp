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
        case VertexLayoutType::Static_Mesh:
        {
            bindings.reserve(4);
            attributes.reserve(4);
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
            static constexpr uint32_t kMaxTexCoordCount = 8;
            for (uint32_t i = 0; i < kMaxTexCoordCount; ++i)
            {
                if (enumHasAnyFlags(mKey.attributeMask, VertexAttributeMask(uint32_t(VertexAttributeMask::TexCoord0) << i)))
                {
                    bindings.push_back({ binding, sizeof(float2), rhi::VertexInputRate::Vertex });
                    attributes.push_back({ binding, binding, rhi::VertexFormat::Float32x2, 0 });
                    ++binding;
                }
            }
            break;
        }
        default:
            break;
        }
    }

    const std::string& VertexLayout::getVertexAttributeGetters() const
    {
        auto& entry = sVertexLayoutTable[mKey];
        if (!entry.vertexAttributeGetters.empty())
        {
            return entry.vertexAttributeGetters;
        }
        // generate getters
        entry.vertexAttributeGetters = generateVertexAttributeGetters();
        return entry.vertexAttributeGetters;
    }

    std::string VertexLayout::generateVertexAttributeGetters() const
    {
        std::string vsInputDefine = "struct VSInput\n{\n";
        std::string getters;

        if (enumHasAllFlags(mKey.attributeMask, VertexAttributeMask::Position))
        {
            vsInputDefine += "    float3 position : POSITION;\n";
            getters += "float3 getPosition(in VSInput vsi) { return vsi.position; }\n";
        }
        else
        {
            getters += "float3 getPosition(in VSInput vsi) { return float3(0); }\n";
        }

        if (enumHasAllFlags(mKey.attributeMask, VertexAttributeMask::Normal))
        {
            vsInputDefine += "    float3 normal : NORMAL;\n";
            getters += "float3 getNormal(in VSInput vsi) { return vsi.normal; }\n";
        }
        else
        {
            getters += "float3 getNormal(in VSInput vsi) { return float3(0, 0, 1); }\n";
        }

        if (enumHasAllFlags(mKey.attributeMask, VertexAttributeMask::Tangent))
        {
            vsInputDefine += "    float4 tangent : TANGENT;\n";
            getters += "float4 getTangent(in VSInput vsi) { return vsi.tangent; }\n";
        }
        else
        {
            getters += "float4 getTangent(in VSInput vsi) { return float4(1, 0, 0, 1); }\n";
        }

        if (enumHasAllFlags(mKey.attributeMask, VertexAttributeMask::Color))
        {
            vsInputDefine += "    float4 color : COLOR;\n";
            getters += "float4 getColor(in VSInput vsi) { return vsi.color; }\n";
        }
        else
        {
            getters += "float4 getColor(in VSInput vsi) { return float4(1); }\n";
        }

        static constexpr VertexAttributeMask kTexCoordMasks[] = {
            VertexAttributeMask::TexCoord0, VertexAttributeMask::TexCoord1,
            VertexAttributeMask::TexCoord2, VertexAttributeMask::TexCoord3,
            VertexAttributeMask::TexCoord4, VertexAttributeMask::TexCoord5,
            VertexAttributeMask::TexCoord6, VertexAttributeMask::TexCoord7,
        };
        for (size_t i = 0; i < sizeof(kTexCoordMasks) / sizeof(kTexCoordMasks[0]); ++i)
        {
            std::string index = std::to_string(i);
            if (enumHasAllFlags(mKey.attributeMask, kTexCoordMasks[i]))
            {
                vsInputDefine += "    float2 texCoord" + index + " : TEXCOORD" + index + ";\n";
                getters += "float2 getTexCoord" + index + "(in VSInput vsi) { return vsi.texCoord" + index + "; }\n";
            }
            else
            {
                getters += "float2 getTexCoord" + index + "(in VSInput vsi) { return float2(0); }\n";
            }
        }

        if (mKey.type == VertexLayoutType::Skeletal_Mesh)
        {
            static constexpr VertexAttributeMask kBoneWeightMasks[] = {
                VertexAttributeMask::Bone_Weight0, VertexAttributeMask::Bone_Weight1,
            };
            for (size_t i = 0; i < sizeof(kBoneWeightMasks) / sizeof(kBoneWeightMasks[0]); ++i)
            {
                std::string index = std::to_string(i);
                if (enumHasAllFlags(mKey.attributeMask, kBoneWeightMasks[i]))
                {
                    vsInputDefine += "    uint4 bone" + index + " : BLENDINDICES" + index + ";\n";
                    vsInputDefine += "    float4 weight" + index + " : BLENDWEIGHT" + index + ";\n";
                    getters += "uint4 getBone" + index + "(in VSInput vsi) { return vsi.bone" + index + "; }\n";
                    getters += "float4 getWeight" + index + "(in VSInput vsi) { return vsi.weight" + index + "; }\n";
                }
                else
                {
                    getters += "uint4 getBone" + index + "(in VSInput vsi) { return uint4(0, 0, 0, 0); }\n";
                    getters += "float4 getWeight" + index + "(in VSInput vsi) { return float4(" + std::string(i == 0 ? "1, 0, 0, 0" : "0, 0, 0, 0") + "); }\n";
                }
            }
        }

        vsInputDefine += "    uint instanceID : SV_VulkanInstanceID;\n";
        getters += "uint getInstanceIndex(in VSInput vsi) { return vsi.instanceID; }\n";

        vsInputDefine += "};\n";
        return vsInputDefine + getters;
    }

}

