#pragma once
#include "../Material/Material.h"
#include <Core/Math/Box.h>
#include <Core/Common/BulkData.h>

namespace hyp
{
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
    template <> Type* Reflection::createType<VertexAttributeType>();

    struct VertexAttribute
    {
        VertexAttributeType type;
        uint32_t stride;
    };
    template <> Type* Reflection::createType<VertexAttribute>();

    enum class IndexType
    {
        Uint16,
        Uint32,
    };
    template <> Type* Reflection::createType<IndexType>();

    class Mesh : public Object
    {
        REFLECT_CLASS(Mesh)
    public:
        Mesh();

        ~Mesh() noexcept;

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

        // 非 const:包围盒是懒计算的,首次调用(或 dirty 之后)可能需要临时 load 顶点 BulkData——
        // 这是有副作用(可能触发磁盘 I/O)的操作,不该伪装成一个廉价的 const getter
        const Boxf& getOrUpdateBoundingBox() noexcept;

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

        void postLoad() override;

        void preSave() override;

    protected:
        void dirtyBound() noexcept
        {
            mBoundDirty = true;
        }

        void updateBound() noexcept;

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
            BulkData vertexData;
            std::vector<VertexAttribute> vertexAttributes;
            BulkData indexData;
            IndexType indexType;
            std::vector<Submesh> submeshes;
            float minScreenPercentage = 0.1f;
        };

        std::vector<LOD> mLODs;
        std::vector<TRef<Material>> mMaterials;

        Boxf mBoundingBox;
        bool mBoundDirty = true;

        std::vector<GeometryProxyHandle> mLODGeometryProxies;
    };

    template <> Type* Reflection::createType<Mesh::Submesh>();

    template <> Type* Reflection::createType<Mesh::LOD>();

    template <> Type* Reflection::createType<Mesh>();
}
