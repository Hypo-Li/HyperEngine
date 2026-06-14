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
        std::vector<VkVertexInputBindingDescription>& bindings,
        std::vector<VkVertexInputAttributeDescription>& attributes
    ) const
    {
        switch (mKey.type)
        {
        case VertexLayoutType::Static_Mesh:
        {
            bindings = {
                { 0, sizeof(float3), VK_VERTEX_INPUT_RATE_VERTEX }, // Position
                { 1, sizeof(float3), VK_VERTEX_INPUT_RATE_VERTEX }, // Normal
                { 2, sizeof(float4), VK_VERTEX_INPUT_RATE_VERTEX }, // Tangent
                { 3, sizeof(float2), VK_VERTEX_INPUT_RATE_VERTEX }, // TexCoord0
            };
            attributes = {
                { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 },
                { 1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0 },
                { 2, 2, VK_FORMAT_R32G32B32A32_SFLOAT, 0 },
                { 3, 3, VK_FORMAT_R32G32_SFLOAT, 0 },
            };
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

        if (enumHasAllFlags(mKey.attributeMask, VertexAttributeMask::TexCoord0))
        {
            vsInputDefine += "    float2 texCoord0 : TEXCOORD0;\n";
            getters += "float2 getTexCoord0(in VSInput vsi) { return vsi.texCoord0; }\n";
        }
        else
        {
            getters += "float2 getTexCoord0(in VSInput vsi) { return float2(0); }\n";
        }

        vsInputDefine += "    uint instanceID : SV_InstanceID;\n    uint startInstanceLocation : SV_StartInstanceLocation;\n";
        getters += "uint getInstanceIndex(in VSInput vsi) { return vsi.instanceID + vsi.startInstanceLocation; }\n";

        vsInputDefine += "};\n";
        return vsInputDefine + getters;
    }

}

