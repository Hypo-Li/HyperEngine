#include "Mesh.h"
#include "../Context.h"
#include "../Physics/PhysicsSystem.h"

#include <RenderBase/RenderFrontend.h>
#include <RenderBase/GeometryProxy.h>

#include <algorithm>
#include <array>

namespace hyp
{
    template <> Type* Reflection::createType<VertexAttributeType>()
    {
        auto e = new TEnumeration<VertexAttributeType>("VertexAttributeType");
        e->addValue("Position", VertexAttributeType::Position);
        e->addValue("Normal", VertexAttributeType::Normal);
        e->addValue("Tangent", VertexAttributeType::Tangent);
        e->addValue("Color", VertexAttributeType::Color);
        e->addValue("TexCoord", VertexAttributeType::TexCoord);
        e->addValue("Bone", VertexAttributeType::Bone);
        e->addValue("Weight", VertexAttributeType::Weight);
        return e;
    }

    template <> Type* Reflection::createType<VertexAttribute>()
    {
        auto s = new TStructure<VertexAttribute>("VertexAttribute");
        s->addProperty("Type", &VertexAttribute::type);
        s->addProperty("Stride", &VertexAttribute::stride);
        return s;
    }

    template <> Type* Reflection::createType<IndexType>()
    {
        auto e = new TEnumeration<IndexType>("IndexType");
        e->addValue("Uint16", IndexType::Uint16);
        e->addValue("Uint32", IndexType::Uint32);
        return e;
    }

    REGISTER_CLASS(Mesh)

    Mesh::~Mesh() noexcept
    {
        for (auto& geometryProxy : mLODGeometryProxies)
        {
            if (geometryProxy.isValid())
            {
                destroyGeometryProxy(geometryProxy);
                Context::get().getRenderFrontend().freeProxyHandle(geometryProxy);
                geometryProxy.reset();
            }
        }

        // PhysicsSystem 可能按 guid 缓存了这个 mesh 的 JPH::Shape。
        Context::get().getPhysicsSystem().invalidateMeshShapeCache(getGuid());
    }

    void Mesh::postLoad()
    {
        // BoundingBox 已经作为普通属性从磁盘读回来了,不需要摸一遍顶点数据重新算——
        // mBoundDirty 本身不参与序列化,反序列化完默认还是构造时的 true,这里手动清掉
        mBoundDirty = false;
    }

    void Mesh::preSave()
    {
        // 如果这份 Mesh 从创建到现在从没被查询过包围盒(mBoundDirty 还是 true),
        // 存盘前必须强制刷新一次,否则 BoundingBox 属性存下去的会是默认构造的 Boxf{},
        // 而不是真实包围盒
        if (mBoundDirty)
            updateBound();
    }

    uint32_t Mesh::addLOD()
    {
        mLODs.emplace_back();
        return static_cast<uint32_t>(mLODs.size() - 1);
    }

    GeometryProxyHandle Mesh::getOrCreateGeometryProxy(uint32_t lodLevel)
    {
        assert(lodLevel < mLODs.size());
        mLODGeometryProxies.resize(mLODs.size());
        auto& renderFrontend = Context::get().getRenderFrontend();
        if (!mLODGeometryProxies[lodLevel].isValid())
        {
            mLODGeometryProxies[lodLevel] = renderFrontend.allocateProxyHandle<ProxyType::Geometry>();
            createGeometryProxy(mLODGeometryProxies[lodLevel], lodLevel);
        }

        return mLODGeometryProxies[lodLevel];
    }

    void Mesh::setLODData(
        uint32_t lodLevel,
        std::vector<uint8_t> vertexData,
        std::vector<VertexAttribute> vertexAttributes,
        std::vector<uint8_t> indexData,
        IndexType indexType,
        float minScreenPercentage
    )
    {
        assert(lodLevel < mLODs.size());
        LOD& lod = mLODs[lodLevel];
        lod.vertexData.setData(std::move(vertexData));
        lod.vertexAttributes = std::move(vertexAttributes);
        lod.indexData.setData(std::move(indexData));
        lod.indexType = indexType;
        lod.minScreenPercentage = minScreenPercentage;

        // 几何内容变了, 之前给这个 lodLevel 缓存的 Mesh Shape(如果有)已经过期。
        Context::get().getPhysicsSystem().invalidateMeshShapeCache(getGuid());
    }

    uint32_t Mesh::addMaterialSlot(Material* material)
    {
        assert(material);
        uint32_t slotIndex = static_cast<uint32_t>(mMaterials.size());
        mMaterials.emplace_back(material);
        notifyMaterialsChanged();
        return slotIndex;
    }

    void Mesh::setMaterialSlot(uint32_t slotIndex, Material* material)
    {
        assert(slotIndex < mMaterials.size());
        if (!material)
        {
            for (const LOD& lod : mLODs)
                for (const Submesh& submesh : lod.submeshes)
                    assert(submesh.materialIndex != slotIndex && "setMaterialSlot(): can't clear a slot still referenced by a Submesh");
        }
        mMaterials[slotIndex] = material;
        notifyMaterialsChanged();
    }

    void Mesh::addMaterialListener(IMeshMaterialListener* listener)
    {
        assert(std::find(mMaterialListeners.begin(), mMaterialListeners.end(), listener) == mMaterialListeners.end());
        mMaterialListeners.push_back(listener);
    }

    void Mesh::removeMaterialListener(IMeshMaterialListener* listener)
    {
        auto it = std::find(mMaterialListeners.begin(), mMaterialListeners.end(), listener);
        assert(it != mMaterialListeners.end());
        mMaterialListeners.erase(it);
    }

    void Mesh::notifyMaterialsChanged()
    {
        for (IMeshMaterialListener* listener : mMaterialListeners)
            listener->onMeshMaterialsChanged();
    }

    void Mesh::addSubmesh(
        uint32_t lodLevel,
        uint32_t vertexOffset,
        uint32_t vertexCount,
        uint32_t indexOffset,
        uint32_t indexCount,
        uint32_t materialSlotIndex
    )
    {
        assert(lodLevel < mLODs.size() && materialSlotIndex < mMaterials.size());

        LOD& lod = mLODs[lodLevel];

        assert(lod.vertexData.getSize() > 0);

        lod.submeshes.emplace_back(
            vertexOffset,
            vertexCount,
            indexOffset,
            indexCount,
            materialSlotIndex
        );

        dirtyBound();
    }

    void Mesh::addSubmesh(
        uint32_t lodLevel,
        uint32_t vertexOffset,
        uint32_t vertexCount,
        uint32_t indexOffset,
        uint32_t indexCount,
        Material* material
    )
    {
        addSubmesh(lodLevel, vertexOffset, vertexCount, indexOffset, indexCount, addMaterialSlot(material));
    }

    const Boxf& Mesh::getOrUpdateBoundingBox() noexcept
    {
        if (mBoundDirty)
            updateBound();
        return mBoundingBox;
    }

    void Mesh::loadLODData(uint32_t lodLevel)
    {
        assert(lodLevel < mLODs.size());
        LOD& lod = mLODs[lodLevel];
        lod.vertexData.load();
        lod.indexData.load();
    }

    void Mesh::unloadLODData(uint32_t lodLevel)
    {
        assert(lodLevel < mLODs.size());
        LOD& lod = mLODs[lodLevel];
        lod.vertexData.unload();
        lod.indexData.unload();
    }

    uint32_t Mesh::getLODVertexCount(uint32_t lodLevel) const noexcept
    {
        assert(lodLevel < mLODs.size());
        uint32_t totalVertexCount = 0;
        for (auto& submesh : mLODs[lodLevel].submeshes)
            totalVertexCount += submesh.vertexCount;
        return totalVertexCount;
    }

    uint32_t Mesh::getLODIndexCount(uint32_t lodLevel) const noexcept
    {
        assert(lodLevel < mLODs.size());
        uint32_t totalIndexCount = 0;
        for (auto& submesh : mLODs[lodLevel].submeshes)
            totalIndexCount += submesh.indexCount;
        return totalIndexCount;
    }

    std::span<const uint8_t> Mesh::getLODAttributeBytes(uint32_t lodLevel, VertexAttributeType attributeType) const noexcept
    {
        const LOD& lod = mLODs[lodLevel];
        assert(lod.vertexData.isLoaded());

        uint32_t totalVertexCount = getLODVertexCount(lodLevel);
        size_t offset = 0;
        for (auto& attribute : lod.vertexAttributes)
        {
            size_t byteCount = static_cast<size_t>(attribute.stride) * totalVertexCount;
            if (attribute.type == attributeType)
                return std::span<const uint8_t>(lod.vertexData.getData().data() + offset, byteCount);
            offset += byteCount;
        }
        return {};
    }

    std::span<const float3> Mesh::LODDataView::getPositions() const noexcept
    {
        std::span<const uint8_t> bytes = mMesh->getLODAttributeBytes(mLODLevel, VertexAttributeType::Position);
        return std::span<const float3>(reinterpret_cast<const float3*>(bytes.data()), bytes.size() / sizeof(float3));
    }

    std::span<const uint32_t> Mesh::LODDataView::getIndices() const
    {
        if (mIndicesCache.empty())
            mIndicesCache = mMesh->getLODIndices(mLODLevel);
        return mIndicesCache;
    }

    std::span<const uint8_t> Mesh::LODDataView::getAttributeBytes(VertexAttributeType attributeType) const noexcept
    {
        return mMesh->getLODAttributeBytes(mLODLevel, attributeType);
    }

    std::vector<uint32_t> Mesh::getLODIndices(uint32_t lodLevel) const
    {
        const LOD& lod = mLODs[lodLevel];
        assert(lod.indexData.isLoaded());

        uint32_t totalIndexCount = getLODIndexCount(lodLevel);
        std::span<const uint8_t> rawData = lod.indexData.getData();
        std::vector<uint32_t> indices(totalIndexCount);
        if (lod.indexType == IndexType::Uint32)
        {
            const uint32_t* src = reinterpret_cast<const uint32_t*>(rawData.data());
            std::copy(src, src + totalIndexCount, indices.begin());
        }
        else
        {
            const uint16_t* src = reinterpret_cast<const uint16_t*>(rawData.data());
            std::copy(src, src + totalIndexCount, indices.begin());
        }
        return indices;
    }

    void Mesh::updateBound() noexcept
    {
        assert(mBoundDirty && mLODs.size() > 0);

        accessLODData(0, [this](const LODDataView& lod)
        {
            float3 min{ std::numeric_limits<float>::max() };
            float3 max{ -std::numeric_limits<float>::max() };
            for (const float3& position : lod.getPositions())
            {
                min = glm::min(min, position);
                max = glm::max(max, position);
            }
            mBoundingBox = Boxf(min, max);
        });

        mBoundDirty = false;
    }

    rhi::IndexType indexTypeToRHIIndexType(IndexType type) noexcept
    {
        switch (type)
        {
        case IndexType::Uint16: return rhi::IndexType::Uint16;
        case IndexType::Uint32: return rhi::IndexType::Uint32;
        default: return rhi::IndexType::Uint16;
        };
    }

    void Mesh::createGeometryProxy(GeometryProxyHandle handle, uint32_t lodLevel)
    {
        LOD& lod = mLODs[lodLevel];
        uint64_t offset = 0;
        uint32_t totalVertexCount = 0;
        for (auto& submesh : lod.submeshes)
            totalVertexCount += submesh.vertexCount;
        VertexAttributeMask vertexAttributeMask = VertexAttributeMask::None;
        uint8_t texCoordOffset = 0;

        // vertexData 里各属性是按 lod.vertexAttributes 的原始(资产里录入的)顺序首尾拼接的,
        // 而 VertexLayout::writeVertexLayoutBindingsAndAttributes() 生成 pipeline 的 binding
        // 顺序是固定的优先级顺序(Position/Normal/Tangent/Color/TexCoord0..3, 再往后是
        // Skeletal_Mesh 才有的 Bone_Weight0/1, 每个 Bone_WeightN 各自是"骨骼索引流紧跟着
        // 权重流"一对), 和资产原始顺序未必一致——所以这里先按原始顺序算出每个属性的字节偏移,
        // 记到按这份固定优先级顺序对齐的表里, 再按那份顺序重新排列输出, 而不是直接按原始
        // 顺序 push。不需要重排 lod.vertexAttributes 本身或搬动 vertexData 的字节, 两边只要
        // 用同一份优先级顺序表就能对齐
        static constexpr uint32_t kFixedAttributeSlotCount = 4 + kMaxTexCoordCount; // Position/Normal/Tangent/Color + TexCoord0..3
        static constexpr uint32_t kMaxBoneWeightGroupCount = 2; // 对应 VertexAttributeMask::Bone_Weight0/1
        static constexpr uint32_t kAttributeSlotCount = kFixedAttributeSlotCount + kMaxBoneWeightGroupCount * 2; // 每组 Bone_WeightN 占"骨骼索引偏移"+"权重偏移"两个槽位
        std::array<uint64_t, kAttributeSlotCount> offsetBySlot{};
        uint32_t boneGroupIndex = 0;
        uint32_t weightGroupIndex = 0;
        for (auto& vertexAttribute : lod.vertexAttributes)
        {
            uint32_t slot = ~0u;
            switch (vertexAttribute.type)
            {
            case VertexAttributeType::Position:
                vertexAttributeMask |= VertexAttributeMask::Position;
                slot = 0;
                break;
            case VertexAttributeType::Normal:
                vertexAttributeMask |= VertexAttributeMask::Normal;
                slot = 1;
                break;
            case VertexAttributeType::Tangent:
                vertexAttributeMask |= VertexAttributeMask::Tangent;
                slot = 2;
                break;
            case VertexAttributeType::Color:
                vertexAttributeMask |= VertexAttributeMask::Color;
                slot = 3;
                break;
            case VertexAttributeType::TexCoord:
                if (texCoordOffset < kMaxTexCoordCount)
                {
                    vertexAttributeMask |= VertexAttributeMask(static_cast<uint32_t>(VertexAttributeMask::TexCoord0) << texCoordOffset);
                    slot = 4 + texCoordOffset;
                    ++texCoordOffset;
                }
                break;
            // Bone/Weight 在资产里是两条独立的流(不像 Position/Normal 那样一个类型对一个
            // mask 位), 第几次出现就属于第几组 Bone_WeightN——mask 位由 Bone 这条流负责置位,
            // 调用方(导入器)必须保证 Bone/Weight 成对出现, Weight 落单不会让这一位生效
            case VertexAttributeType::Bone:
                if (boneGroupIndex < kMaxBoneWeightGroupCount)
                {
                    vertexAttributeMask |= VertexAttributeMask(static_cast<uint32_t>(VertexAttributeMask::Bone_Weight0) << boneGroupIndex);
                    slot = kFixedAttributeSlotCount + boneGroupIndex * 2;
                    ++boneGroupIndex;
                }
                break;
            case VertexAttributeType::Weight:
                if (weightGroupIndex < kMaxBoneWeightGroupCount)
                {
                    slot = kFixedAttributeSlotCount + weightGroupIndex * 2 + 1;
                    ++weightGroupIndex;
                }
                break;
            default:
                break;
            }

            if (slot != ~0u)
            {
                offsetBySlot[slot] = offset;
            }
            offset += vertexAttribute.stride * totalVertexCount;
        }

        std::vector<uint64_t> vertexBindingOffsets;
        vertexBindingOffsets.reserve(kAttributeSlotCount);
        for (uint32_t slot = 0; slot < kFixedAttributeSlotCount; ++slot)
        {
            if (enumHasAnyFlags(vertexAttributeMask, VertexAttributeMask(1u << slot)))
            {
                vertexBindingOffsets.emplace_back(offsetBySlot[slot]);
            }
        }
        for (uint32_t group = 0; group < kMaxBoneWeightGroupCount; ++group)
        {
            VertexAttributeMask groupMask = VertexAttributeMask(static_cast<uint32_t>(VertexAttributeMask::Bone_Weight0) << group);
            if (enumHasAnyFlags(vertexAttributeMask, groupMask))
            {
                vertexBindingOffsets.emplace_back(offsetBySlot[kFixedAttributeSlotCount + group * 2]);
                vertexBindingOffsets.emplace_back(offsetBySlot[kFixedAttributeSlotCount + group * 2 + 1]);
            }
        }
        std::vector<GeometryProxy::Section> sections;
        for (auto& submesh : lod.submeshes)
        {
            GeometryProxy::Section section;
            section.vertexOffset = submesh.vertexOffset;
            section.firstIndex = submesh.indexOffset;
            section.indexCount = submesh.indexCount;
            sections.emplace_back(section);
        }

        // 上传 GPU 只需要这一份拷贝,拷完就不再需要 CPU 侧常驻——主动 unload 掉,
        // 不用等渲染线程真正消费完这份拷贝(它已经是独立的一份数据了)
        lod.vertexData.load();
        lod.indexData.load();
        std::vector<uint8_t> vertexData(lod.vertexData.getData().begin(), lod.vertexData.getData().end());
        std::vector<uint8_t> indexData(lod.indexData.getData().begin(), lod.indexData.getData().end());
        lod.vertexData.unload();
        lod.indexData.unload();

        VertexLayoutType layoutType = enumHasAnyFlags(vertexAttributeMask, VertexAttributeMask::Bone_Weight0 | VertexAttributeMask::Bone_Weight1)
            ? VertexLayoutType::Skeletal_Mesh
            : VertexLayoutType::Static_Mesh;

        Context::get().getRenderFrontend().enqueueLambda(
            [this,
            handle,
            layoutType,
            vertexAttributeMask,
            vertexData = std::move(vertexData),
            vertexBindingOffsets = std::move(vertexBindingOffsets),
            indexData = std::move(indexData),
            indexType = indexTypeToRHIIndexType(lod.indexType),
            sections = std::move(sections)](FrameContext& context) mutable
            {
                context.getProxyManager().createProxy(
                    handle,
                    VertexLayout(layoutType, vertexAttributeMask),
                    std::move(vertexData),
                    std::move(vertexBindingOffsets),
                    std::move(indexData),
                    indexType,
                    std::move(sections)
                );
            }
        );
    }

    void Mesh::destroyGeometryProxy(GeometryProxyHandle handle) noexcept
    {
        assert(handle.isValid());
        Context::get().getRenderFrontend().enqueueLambda(
            [handle](FrameContext& context)
            {
                context.getProxyManager().destroyProxy(handle);
            }
        );
    }

    size_t Mesh::getLODAttributeByteOffset(uint32_t lodLevel, VertexAttributeType attributeType) const noexcept
    {
        assert(lodLevel < mLODs.size());
        const LOD& lod = mLODs[lodLevel];
        uint32_t totalVertexCount = getLODVertexCount(lodLevel);

        size_t offset = 0;
        for (auto& vertexAttribute : lod.vertexAttributes)
        {
            if (vertexAttribute.type == attributeType)
                break;
            offset += vertexAttribute.stride * totalVertexCount;
        }
        return offset;
    }

    template <> Type* Reflection::createType<Mesh::Submesh>()
    {
        auto s = new TStructure<Mesh::Submesh>("Mesh.Submesh");
        s->addProperty("VertexOffset", &Mesh::Submesh::vertexOffset);
        s->addProperty("VertexCount", &Mesh::Submesh::vertexCount);
        s->addProperty("IndexOffset", &Mesh::Submesh::indexOffset);
        s->addProperty("IndexCount", &Mesh::Submesh::indexCount);
        s->addProperty("MaterialIndex", &Mesh::Submesh::materialIndex);
        return s;
    }

    template <> Type* Reflection::createType<Mesh::LOD>()
    {
        auto s = new TStructure<Mesh::LOD>("Mesh.LOD");
        s->addProperty("VertexData", &Mesh::LOD::vertexData);
        s->addProperty("VertexAttributes", &Mesh::LOD::vertexAttributes);
        s->addProperty("IndexData", &Mesh::LOD::indexData);
        s->addProperty("IndexType", &Mesh::LOD::indexType);
        s->addProperty("Submeshes", &Mesh::LOD::submeshes);
        s->addProperty("MinScreenPercentage", &Mesh::LOD::minScreenPercentage);
        return s;
    }

    template <> Type* Reflection::createType<Mesh>()
    {
        auto c = new TClass<Mesh>("Mesh");
        c->addProperty("LODs", &Mesh::mLODs);
        c->addProperty("Materials", &Mesh::mMaterials);
        c->addProperty("BoundingBox", &Mesh::mBoundingBox);
        return c;
    }
}
