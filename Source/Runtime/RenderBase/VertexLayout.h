#pragma once
#include "Vulkan/Pipeline.h"

#include <Core/Common/EnumBitwise.h>
#include <Core/Common/Hash.h>

namespace hyp
{
    enum class VertexLayoutType : uint8_t
    {
        Unknown = static_cast<uint8_t>(-1),
        Static_Mesh = 0,
    };

    enum class VertexAttributeMask : uint32_t
    {
        None            = 0,
        Position        = 1 << 0,
        Normal          = 1 << 1,
        Tangent         = 1 << 2,
        Color           = 1 << 3,
        TexCoord0       = 1 << 4,
        TexCoord1       = 1 << 5,
        TexCoord2       = 1 << 6,
        TexCoord3       = 1 << 7,
        TexCoord4       = 1 << 8,
        TexCoord5       = 1 << 9,
        TexCoord6       = 1 << 10,
        TexCoord7       = 1 << 11,
        Bone_Weight0    = 1 << 12,
        Bone_Weight1    = 1 << 13,
    };
    ENABLE_ENUM_CLASS_BITWISE(VertexAttributeMask)

    class VertexLayout
    {
    public:
        VertexLayout() = default;

        VertexLayout(VertexLayoutType type, VertexAttributeMask attributeMask);

        bool operator==(const VertexLayout& other) const noexcept
        {
            return mId == other.mId;
        }

        VertexLayoutType getType() const noexcept
        {
            return mKey.type;
        }

        VertexAttributeMask getAttributeMask() const noexcept
        {
            return mKey.attributeMask;
        }

        uint32_t getId() const noexcept
        {
            return mId;
        }

        void writeVertexLayoutBindingsAndAttributes(
            std::vector<VkVertexInputBindingDescription>& bindings,
            std::vector<VkVertexInputAttributeDescription>& attributes
        ) const;

        const std::string& getVertexAttributeGetters() const;

    private:
        std::string generateVertexAttributeGetters() const;

    private:
        struct VertexLayoutKey
        {
            VertexLayoutType type = VertexLayoutType::Unknown;
            VertexAttributeMask attributeMask = VertexAttributeMask::None;

            bool operator==(const VertexLayoutKey& other) const noexcept
            {
                return type == other.type && attributeMask == other.attributeMask;
            }
        };

        VertexLayoutKey mKey;
        uint32_t mId = ~0u;

        struct VertexLayoutKeyHash
        {
            std::size_t operator()(const VertexLayoutKey& key) const noexcept
            {
                size_t seed = 0;
                Hash::combine(seed, key.type);
                Hash::combine(seed, key.attributeMask);
                return seed;
            }
        };

        struct VertexLayoutInfo
        {
            uint32_t id;
            std::string vertexAttributeGetters;
        };

        using VertexLayoutTable = std::unordered_map<VertexLayoutKey, VertexLayoutInfo, VertexLayoutKeyHash>;
        static VertexLayoutTable sVertexLayoutTable;
    };
}

namespace std
{
    template<>
    struct hash<hyp::VertexLayout>
    {
        std::size_t operator()(const hyp::VertexLayout& vertexLayout) const noexcept
        {
            return std::hash<uint32_t>{}(vertexLayout.getId());
        }
    };
}
