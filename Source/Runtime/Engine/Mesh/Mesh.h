#pragma once
#include "../Material/Material.h"
#include <Core/Math/Box.h>
#include <Core/Common/BulkData.h>
#include <RenderBase/ProxyHandle.h>

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

    class IMeshMaterialListener
    {
    public:
        virtual void onMeshMaterialsChanged() = 0;

    protected:
        ~IMeshMaterialListener() = default;
    };

    // LOD 顶点/索引数据、submesh/材质槽位管理、GPU 几何代理——静态网格和骨架网格都需要的部分,
    // GeometryProxy 只是把 LOD 的顶点/索引字节内容搬上 GPU, 跟形状会不会变(蒙不蒙皮)无关。
    // 距离场/Mesh Card 这些确实只对形状固定不变的静态网格成立的东西还是在 StaticMesh 里
    class Mesh : public Object
    {
        REFLECT_CLASS(Mesh)
    public:
        Mesh() = default;

        ~Mesh() noexcept override;

        void postLoad() override;

        void preSave() override;

        uint32_t addLOD();

        GeometryProxyHandle getOrCreateGeometryProxy(uint32_t lodLevel);

        void setLODData(
            uint32_t lodLevel,
            std::vector<uint8_t> vertexData,
            std::vector<VertexAttribute> vertexAttributes,
            std::vector<uint8_t> indexData,
            IndexType indexType,
            float minScreenPercentage
        );

        // 总是新建一个材质槽位, 不做任何基于材质指针的去重——多个子网格如果要共享同一个
        // 槽位(比如多级 LOD 里同一个概念部件), 由调用方显式复用同一个 slotIndex, 不再靠
        // "两次传的材质指针恰好相等"这种隐式判断
        uint32_t addMaterialSlot(Material* material);

        // 把一个已存在的槽位重新指向另一个材质, 槽位索引本身不变——"先用占位材质导入,
        // 之后再逐槽位替换成真实材质"这类场景走这个。material 传 nullptr 相当于释放这个
        // 槽位持有的引用, 调用前必须保证没有任何 Submesh 还在引用这个槽位(否则 assert);
        // 槽位索引本身继续占位, 不会被后续 addMaterialSlot() 复用, 也不会导致其它槽位的
        // 索引发生偏移
        void setMaterialSlot(uint32_t slotIndex, Material* material);

        void addMaterialListener(IMeshMaterialListener* listener);

        void removeMaterialListener(IMeshMaterialListener* listener);

        void addSubmesh(
            uint32_t lodLevel,
            uint32_t vertexOffset,
            uint32_t vertexCount,
            uint32_t indexOffset,
            uint32_t indexCount,
            uint32_t materialSlotIndex
        );

        // 便捷重载: 总是新建一个槽位再挂到这个子网格上——适合没有跨子网格共享材质槽位
        // 需求的调用方; 需要共享槽位时改用上面接收 materialSlotIndex 的版本。传 TRef<T> 时
        // 要显式 .get(): TRef 的 operator bool() 会和 materialSlotIndex 版本产生二义性重载
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

        // 所有 span 只在 accessLODData() 本次回调期间有效
        class LODDataView
        {
        public:
            std::span<const float3> getPositions() const noexcept;

            std::span<const uint32_t> getIndices() const;

        private:
            friend class Mesh;
            LODDataView(const Mesh* mesh, uint32_t lodLevel) noexcept
                : mMesh(mesh), mLODLevel(lodLevel)
            {
            }

            // 该 LOD 没有这个属性时返回空 span
            std::span<const uint8_t> getAttributeBytes(VertexAttributeType attributeType) const noexcept;

            const Mesh* mMesh;
            uint32_t mLODLevel;
            mutable std::vector<uint32_t> mIndicesCache;
        };

        template <typename F>
        void accessLODData(uint32_t lodLevel, F&& fn)
        {
            assert(lodLevel < mLODs.size());
            bool wasAlreadyLoaded = mLODs[lodLevel].vertexData.isLoaded();
            if (!wasAlreadyLoaded)
                loadLODData(lodLevel);
            fn(LODDataView(this, lodLevel));
            if (!wasAlreadyLoaded)
                unloadLODData(lodLevel);
        }

        // 纯元数据, 不涉及 BulkData 字节内容, 不需要先 accessLODData()
        uint32_t getLODVertexCount(uint32_t lodLevel) const noexcept;

        uint32_t getLODIndexCount(uint32_t lodLevel) const noexcept;

        int32_t getSubmeshVertexOffset(uint32_t lodLevel, uint32_t submeshIndex) const noexcept
        {
            assert(lodLevel < mLODs.size());
            auto& lod = mLODs[lodLevel];
            assert(submeshIndex < lod.submeshes.size());
            return lod.submeshes[submeshIndex].vertexOffset;
        }

        uint32_t getSubmeshVertexCount(uint32_t lodLevel, uint32_t submeshIndex) const noexcept
        {
            assert(lodLevel < mLODs.size());
            auto& lod = mLODs[lodLevel];
            assert(submeshIndex < lod.submeshes.size());
            return lod.submeshes[submeshIndex].vertexCount;
        }

        uint32_t getSubmeshIndexOffset(uint32_t lodLevel, uint32_t submeshIndex) const noexcept
        {
            assert(lodLevel < mLODs.size());
            auto& lod = mLODs[lodLevel];
            assert(submeshIndex < lod.submeshes.size());
            return lod.submeshes[submeshIndex].indexOffset;
        }

        uint32_t getSubmeshIndexCount(uint32_t lodLevel, uint32_t submeshIndex) const noexcept
        {
            assert(lodLevel < mLODs.size());
            auto& lod = mLODs[lodLevel];
            assert(submeshIndex < lod.submeshes.size());
            return lod.submeshes[submeshIndex].indexCount;
        }

    protected:
        void dirtyBound() noexcept
        {
            mBoundDirty = true;
        }

        void updateBound() noexcept;

        void createGeometryProxy(GeometryProxyHandle handle, uint32_t lodLevel);

        void destroyGeometryProxy(GeometryProxyHandle handle) noexcept;

        // 不对外暴露, 避免调用方绕开配对好的 load/unload 逻辑
        void loadLODData(uint32_t lodLevel);

        void unloadLODData(uint32_t lodLevel);

        // 某个顶点属性在 lod.vertexData 里的起始字节偏移——vertexData 是按
        // lod.vertexAttributes 的顺序把各属性首尾拼接的 SoA 布局(每个属性是连续
        // 一整块, 覆盖该 LOD 全部顶点), 不是逐顶点交错的 AoS, 所以偏移只取决于
        // "目标属性之前那些属性各自的 stride * 总顶点数"
        size_t getLODAttributeByteOffset(uint32_t lodLevel, VertexAttributeType attributeType) const noexcept;

        // 调用前必须保证 lod.vertexData 已经是 Loaded 状态
        std::span<const uint8_t> getLODAttributeBytes(uint32_t lodLevel, VertexAttributeType attributeType) const noexcept;

        // 调用前必须保证 lod.indexData 已经是 Loaded 状态
        std::vector<uint32_t> getLODIndices(uint32_t lodLevel) const;

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

    private:
        void notifyMaterialsChanged();

        Boxf mBoundingBox;
        bool mBoundDirty = true;
        std::vector<GeometryProxyHandle> mLODGeometryProxies;
        std::vector<IMeshMaterialListener*> mMaterialListeners;
    };

    template <> Type* Reflection::createType<Mesh::Submesh>();

    template <> Type* Reflection::createType<Mesh::LOD>();

    template <> Type* Reflection::createType<Mesh>();
}
