#pragma once
#include "../Material/Material.h"
#include <Core/Math/Box.h>

namespace hyp
{
    class Mesh : public Object
    {
        REFLECT_CLASS(Mesh)
    public:
        Mesh();

        ~Mesh() noexcept;

        enum class VertexAttributeType
        {
            Position,
            Normal,
            Tangent,
            Color,
            TexCoord,
            Bone,
            Weight,
        };

        struct VertexAttribute
        {
            VertexAttributeType type;
            uint32_t stride;
        };

        enum class IndexType
        {
            Uint16,
            Uint32,
        };

        uint32_t addLOD();

        void setLODData(
            uint32_t lodLevel,
            std::vector<uint8_t> vertexData,
            std::vector<VertexAttribute> vertexAttributes,
            std::vector<uint8_t> indexData,
            IndexType indexType,
            float minScreenPercentage
        );

        void addSubmesh(
            uint32_t lodLevel,
            uint32_t vertexOffset,
            uint32_t vertexCount,
            uint32_t indexOffset,
            uint32_t indexCount,
            Material* material
        );

        uint32_t getLODCount() const noexcept
        {
            return static_cast<uint32_t>(mLODs.size());
        }

        float getLODMinScreenPercentage(uint32_t lodLevel) const noexcept
        {
            assert(lodLevel < mLODs.size());
            return mLODs[lodLevel].minScreenPercentage;
        }

        uint32_t getSubmeshCount(uint32_t lodLevel) const noexcept
        {
            assert(lodLevel < mLODs.size());
            return static_cast<uint32_t>(mLODs[lodLevel].submeshes.size());
        }

        uint32_t getSubmeshMaterialIndex(uint32_t lodLevel, uint32_t submeshIndex) const noexcept
        {
            assert(lodLevel < mLODs.size());
            auto& lod = mLODs[lodLevel];
            assert(submeshIndex < lod.submeshes.size());
            return lod.submeshes[submeshIndex].materialIndex;
        }

        const Boxf& getBoundingBox() const noexcept;

        uint32_t getMaterialCount() const noexcept
        {
            return static_cast<uint32_t>(mMaterials.size());
        }

        Material* getMaterial(uint32_t index) noexcept
        {
            assert(index < mMaterials.size());
            return mMaterials[index].get();
        }

        GeometryProxyHandle getOrCreateGeometryProxy(uint32_t lodLevel);

    protected:
        void dirtyBound() noexcept
        {
            mBoundDirty = true;
        }

        void updateBound() const noexcept;

        void createGeometryProxy(GeometryProxyHandle handle, uint32_t lodLevel);

        void destroyGeometryProxy(GeometryProxyHandle handle) noexcept;

    private:
        struct Submesh
        {
            int32_t vertexOffset;
            uint32_t vertexCount;
            uint32_t indexOffset;
            uint32_t indexCount;
            uint32_t materialIndex;
        };

        struct LOD
        {
            std::vector<uint8_t> vertexData;
            std::vector<VertexAttribute> vertexAttributes;
            std::vector<uint8_t> indexData;
            IndexType indexType;
            std::vector<Submesh> submeshes;
            float minScreenPercentage = 0.1f;
        };

        std::vector<LOD> mLODs;
        std::vector<GeometryProxyHandle> mLODGeometryProxies;
        std::vector<TRef<Material>> mMaterials;

        mutable Boxf mBoundingBox;
        mutable bool mBoundDirty = true;
    };

    template <> Type* Reflection::createType<Mesh>();
}
