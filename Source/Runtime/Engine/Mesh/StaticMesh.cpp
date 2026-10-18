#include "StaticMesh.h"
#include "MeshUtility.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#include <RenderBase/RenderFrontend.h>
#include <RenderBase/GeometryProxy.h>
#include <RenderBase/MeshShapeProxy.h>
#include <RenderBase/DistanceFieldBrickAtlas.h>
#include <Core/Common/DerivedDataCache.h>
#include <Core/Common/LogSystem.h>
#include <Core/Common/ModuleServiceRegistry.h>
#include <Platform/Platform.h>
#include "../Context.h"

namespace
{
    // 有符号距离值 <-> R8_UNORM 字节的编解码, 对称线性映射 [-range,range] -> [0,255]。
    // range 取窄带宽度本身——窄带内的值按定义不会超出这个范围(见 querySignedDistance()),
    // 这是精确上下界, 不是留了余量的估计值。shader 端(SparseDistanceField.slang/
    // GlobalDistanceFieldQuery.slang/GlobalDistanceFieldComposite.slang)要手写一份数值上
    // 完全对称的解码/编码, 改这里的公式时同步改那几处
    uint8_t encodeSignedDistance(float value, float range) noexcept
    {
        float t = std::clamp(value / range, -1.0f, 1.0f);
        return static_cast<uint8_t>(std::lround((t * 0.5f + 0.5f) * 255.0f));
    }
}

namespace hyp
{
    REGISTER_CLASS(StaticMesh)

    StaticMesh::~StaticMesh() noexcept
    {
        if (mMeshShapeProxy.isValid())
        {
            destroyMeshShapeProxy(mMeshShapeProxy);
            Context::get().getRenderFrontend().freeProxyHandle(mMeshShapeProxy);
            mMeshShapeProxy.reset();
        }
    }

    void StaticMesh::setDistanceFieldData(int3 resolution, float3 boundsMin, float3 boundsMax, float narrowBandDistance, std::vector<float> denseDistances)
    {
        const int3 sampleCount = resolution + 1;
        assert(static_cast<size_t>(sampleCount.x) * sampleCount.y * sampleCount.z == denseDistances.size());

        const int3 brickGridResolution(
            (resolution.x + kDistanceFieldUniqueBrickSize - 1) / kDistanceFieldUniqueBrickSize,
            (resolution.y + kDistanceFieldUniqueBrickSize - 1) / kDistanceFieldUniqueBrickSize,
            (resolution.z + kDistanceFieldUniqueBrickSize - 1) / kDistanceFieldUniqueBrickSize
        );
        const size_t brickGridCount = static_cast<size_t>(brickGridResolution.x) * brickGridResolution.y * brickGridResolution.z;

        auto denseIndex = [&](int32_t x, int32_t y, int32_t z) noexcept -> size_t
        {
            return (static_cast<size_t>(z) * sampleCount.y + y) * static_cast<size_t>(sampleCount.x) + x;
        };

        std::vector<uint32_t> brickIndirection(brickGridCount, kInvalidDistanceFieldBrickIndex);
        std::vector<uint8_t> packedBricks;
        std::vector<uint8_t> brickScratch(kDistanceFieldBrickVoxelCount);

        for (int32_t bz = 0; bz < brickGridResolution.z; ++bz)
        {
            for (int32_t by = 0; by < brickGridResolution.y; ++by)
            {
                for (int32_t bx = 0; bx < brickGridResolution.x; ++bx)
                {
                    bool brickHasSurfaceInfo = false;
                    for (int32_t lz = 0; lz < kDistanceFieldBrickSize; ++lz)
                    {
                        // 块间按 kDistanceFieldUniqueBrickSize(7)为步长排列, 不是 kDistanceFieldBrickSize(8)——
                        // 每块的第 7 层(lz/ly/lx == kDistanceFieldUniqueBrickSize)跟下一块的第 0 层
                        // 是网格坐标上同一个位置, 两块各自都会存一份, 这就是留给三线性插值
                        // 用的重叠边界
                        int32_t z = bz * kDistanceFieldUniqueBrickSize + lz;
                        for (int32_t ly = 0; ly < kDistanceFieldBrickSize; ++ly)
                        {
                            int32_t y = by * kDistanceFieldUniqueBrickSize + ly;
                            for (int32_t lx = 0; lx < kDistanceFieldBrickSize; ++lx)
                            {
                                int32_t x = bx * kDistanceFieldUniqueBrickSize + lx;
                                // 越界体素(分辨率不是 kDistanceFieldUniqueBrickSize 整数倍时, 最外层块
                                // 会超出 resolution)填窄带常量占位——反正不会被采样到, 只是
                                // 为了让每块都是定长
                                float value = narrowBandDistance;
                                if (x < sampleCount.x && y < sampleCount.y && z < sampleCount.z)
                                    value = denseDistances[denseIndex(x, y, z)];

                                size_t localIndex = (static_cast<size_t>(lz) * kDistanceFieldBrickSize + ly) * kDistanceFieldBrickSize + lx;
                                // "是否有效"必须拿原始 float 比较, 不能拿量化后的字节比较——
                                // 不同的原始值可能量化到同一个字节, 拿字节比较会有精度混淆
                                // 导致误判的风险
                                brickScratch[localIndex] = encodeSignedDistance(value, narrowBandDistance);
                                if (value != narrowBandDistance)
                                    brickHasSurfaceInfo = true;
                            }
                        }
                    }

                    if (brickHasSurfaceInfo)
                    {
                        size_t brickIndex = packedBricks.size() / kDistanceFieldBrickVoxelCount;
                        brickIndirection[(static_cast<size_t>(bz) * brickGridResolution.y + by) * brickGridResolution.x + bx] = static_cast<uint32_t>(brickIndex);
                        packedBricks.insert(packedBricks.end(), brickScratch.begin(), brickScratch.end());
                    }
                }
            }
        }

        const size_t packedBrickCount = packedBricks.size() / kDistanceFieldBrickVoxelCount;
        LOG_INFO(
            "StaticMesh::setDistanceFieldData: resolution {}x{}x{}, {} / {} bricks kept ({:.1f}%), brickData {:.1f} KB vs dense {:.1f} KB ({:.1f}x)",
            resolution.x, resolution.y, resolution.z,
            packedBrickCount, brickGridCount,
            brickGridCount > 0 ? 100.0 * static_cast<double>(packedBrickCount) / static_cast<double>(brickGridCount) : 0.0,
            static_cast<double>(packedBricks.size()) / 1024.0,
            static_cast<double>(denseDistances.size() * sizeof(float)) / 1024.0,
            packedBricks.empty() ? 0.0 : static_cast<double>(denseDistances.size()) / static_cast<double>(packedBricks.size())
        );

        DistanceFieldData data;
        data.resolution = resolution;
        data.bounds = Boxf(boundsMin, boundsMax);
        data.narrowBandDistance = narrowBandDistance;
        data.brickGridResolution = brickGridResolution;
        data.brickIndirection = std::move(brickIndirection);
        data.brickData.setData(std::move(packedBricks));

        mDistanceFieldData = std::move(data);
    }

    void StaticMesh::setCardData(Boxf meshCardsBounds, bool mostlyTwoSided, std::vector<MeshCard> cards)
    {
        CardData data;
        data.meshCardsBounds = meshCardsBounds;
        data.mostlyTwoSided = mostlyTwoSided;
        data.cards = std::move(cards);
        mCardData = std::move(data);
    }

    void StaticMesh::bakeCardData()
    {
        IMeshUtility* meshUtility = ModuleServiceRegistry::get().getService<IMeshUtility>();
        if (!meshUtility)
            return; // 运行时/未链接 Editor 工具链, 没法烘焙

        MeshCardBakeParams params;
        MeshCardBakeResult result = meshUtility->bakeCards(this, params);
        setCardData(result.meshCardsBounds, result.mostlyTwoSided, std::move(result.cards));
    }

    void StaticMesh::ensureDistanceFieldData()
    {
        if (mDistanceFieldData)
            return;

        DerivedDataCache distanceFieldCache(Platform::getExecutableDir() / "Cache", "Meshes");
        uint128_t cacheKey = computeDistanceFieldCacheKey();
        std::optional<std::vector<uint8_t>> cached = distanceFieldCache.tryLoad(cacheKey);
        if (cached)
        {
            unpackDistanceFieldData(*cached);
            return;
        }

        IMeshUtility* meshUtility = ModuleServiceRegistry::get().getService<IMeshUtility>();
        if (!meshUtility)
            return; // 运行时/未链接 Editor 工具链, 没法烘焙——跟 Texture::ensureMips() 对 ITextureUtility 缺失的处理一致

        MeshSDFBakeParams params;
        MeshSDFBakeResult result = meshUtility->bakeDistanceField(this, params);
        setDistanceFieldData(result.resolution, result.boundsMin, result.boundsMax, result.narrowBandDistance, std::move(result.denseDistances));

        // 写回一份供下次 ensureDistanceFieldData() 命中, 不然每次都要重新烘焙(照抄 Texture::bakeMips() 的写回时机)
        distanceFieldCache.save(cacheKey, packDistanceFieldData());
    }

    uint128_t StaticMesh::computeDistanceFieldCacheKey()
    {
        assert(!mLODs.empty());
        LOD& lod0 = mLODs[0];

        bool wasAlreadyLoaded = lod0.vertexData.isLoaded();
        if (!wasAlreadyLoaded)
        {
            lod0.vertexData.load();
            lod0.indexData.load();
        }

        uint128_t key = Hash::compute(lod0.vertexData.getData().data(), lod0.vertexData.getData().size(), Hash::Algorithm128::xxHash);
        Hash::combine(key, Hash::compute(lod0.indexData.getData().data(), lod0.indexData.getData().size(), Hash::Algorithm128::xxHash));

        // 对应 UE DISTANCEFIELD_DERIVEDDATA_VER
        constexpr uint32_t kDistanceFieldDerivedDataVersion = 5;
        Hash::combine(key, kDistanceFieldDerivedDataVersion);

        const MeshSDFBakeParams defaultParams;
        Hash::combine(key, defaultParams.voxelDensity);
        Hash::combine(key, defaultParams.maxResolutionPerAxis);
        Hash::combine(key, defaultParams.narrowBandVoxels);
        Hash::combine(key, mDistanceFieldResolutionScale);

        if (!wasAlreadyLoaded)
        {
            lod0.vertexData.unload();
            lod0.indexData.unload();
        }

        return key;
    }

    std::vector<uint8_t> StaticMesh::packDistanceFieldData()
    {
        assert(mDistanceFieldData);
        DistanceFieldData& data = *mDistanceFieldData;

        bool wasAlreadyLoaded = data.brickData.isLoaded();
        if (!wasAlreadyLoaded)
            data.brickData.load();
        std::span<const uint8_t> brickBytes = data.brickData.getData();

        std::vector<uint8_t> blob;
        auto appendBytes = [&blob](const void* ptr, size_t size)
        {
            const uint8_t* bytes = static_cast<const uint8_t*>(ptr);
            blob.insert(blob.end(), bytes, bytes + size);
        };

        appendBytes(&data.resolution, sizeof(data.resolution));
        appendBytes(&data.bounds, sizeof(data.bounds));
        appendBytes(&data.narrowBandDistance, sizeof(data.narrowBandDistance));
        appendBytes(&data.brickGridResolution, sizeof(data.brickGridResolution));

        uint32_t brickIndirectionCount = static_cast<uint32_t>(data.brickIndirection.size());
        appendBytes(&brickIndirectionCount, sizeof(brickIndirectionCount));
        appendBytes(data.brickIndirection.data(), data.brickIndirection.size() * sizeof(uint32_t));

        uint32_t brickBytesSize = static_cast<uint32_t>(brickBytes.size());
        appendBytes(&brickBytesSize, sizeof(brickBytesSize));
        appendBytes(brickBytes.data(), brickBytes.size());

        if (!wasAlreadyLoaded)
            data.brickData.unload();

        return blob;
    }

    void StaticMesh::unpackDistanceFieldData(std::span<const uint8_t> blob)
    {
        DistanceFieldData data;

        size_t offset = 0;
        auto readBytes = [&](void* dst, size_t size) -> bool
        {
            if (offset + size > blob.size())
                return false;
            std::memcpy(dst, blob.data() + offset, size);
            offset += size;
            return true;
        };

        uint32_t brickIndirectionCount = 0;
        uint32_t brickBytesSize = 0;
        if (!readBytes(&data.resolution, sizeof(data.resolution)) ||
            !readBytes(&data.bounds, sizeof(data.bounds)) ||
            !readBytes(&data.narrowBandDistance, sizeof(data.narrowBandDistance)) ||
            !readBytes(&data.brickGridResolution, sizeof(data.brickGridResolution)) ||
            !readBytes(&brickIndirectionCount, sizeof(brickIndirectionCount)))
        {
            return; // 损坏的缓存文件——保持 mDistanceFieldData 为空, 下次 ensureDistanceFieldData() 会当成未命中重新烘焙
        }

        data.brickIndirection.resize(brickIndirectionCount);
        if (!readBytes(data.brickIndirection.data(), brickIndirectionCount * sizeof(uint32_t)) ||
            !readBytes(&brickBytesSize, sizeof(brickBytesSize)) ||
            offset + brickBytesSize > blob.size())
        {
            return;
        }

        std::vector<uint8_t> brickBytes(blob.begin() + offset, blob.begin() + offset + brickBytesSize);
        data.brickData.setData(std::move(brickBytes));

        mDistanceFieldData = std::move(data);
    }

    MeshShapeProxyHandle StaticMesh::getOrCreateMeshShapeProxy()
    {
        auto& renderFrontend = Context::get().getRenderFrontend();
        if (!mMeshShapeProxy.isValid())
        {
            mMeshShapeProxy = renderFrontend.allocateProxyHandle<ProxyType::Mesh_Shape>();
            createMeshShapeProxy(mMeshShapeProxy);
        }

        return mMeshShapeProxy;
    }

    void StaticMesh::createMeshShapeProxy(MeshShapeProxyHandle handle)
    {
        // 创建 MeshShapeProxy 需要保证拥有 Distance Field 数据
        ensureDistanceFieldData();

        // MeshShapeProxy 的构造函数在渲染线程跑, 这里先在调用方线程把要传给它的数据
        // 拷出一份纯值快照(不含 BulkData 这种延迟加载包装), 避免渲染线程反过来摸
        // StaticMesh 的私有状态
        MeshShapeProxy::DistanceFieldInput dfInput;
        {
            dfInput.resolution = mDistanceFieldData->resolution;
            dfInput.bounds = mDistanceFieldData->bounds;
            dfInput.narrowBandDistance = mDistanceFieldData->narrowBandDistance;
            dfInput.brickGridResolution = mDistanceFieldData->brickGridResolution;
            dfInput.brickIndirection = mDistanceFieldData->brickIndirection;

            BulkData& brickData = mDistanceFieldData->brickData;
            bool wasAlreadyLoaded = brickData.isLoaded();
            if (!wasAlreadyLoaded)
                brickData.load();
            std::span<const uint8_t> rawBrickData = brickData.getData();
            dfInput.brickData.assign(rawBrickData.begin(), rawBrickData.end());
            if (!wasAlreadyLoaded)
                brickData.unload();
        }

        // mCardData.cards 已经是 MeshShapeProxy 构造函数要的类型
        // (hyp::MeshCard 是 CPU 资产格式和 GPU 打包格式共用的同一份 struct, 见
        // RenderBase/MeshShapeTypes.h), 不需要逐字段转换
        std::vector<MeshCard> cards = mCardData.cards;

        Context::get().getRenderFrontend().enqueueLambda(
            [handle, distanceFieldInput = std::move(dfInput), cards = std::move(cards)](FrameContext& context) mutable
            {
                context.getProxyManager().createProxy(handle, std::move(distanceFieldInput), std::move(cards));
            }
        );
    }

    void StaticMesh::destroyMeshShapeProxy(MeshShapeProxyHandle handle) noexcept
    {
        assert(handle.isValid());
        Context::get().getRenderFrontend().enqueueLambda(
            [handle](FrameContext& context)
            {
                context.getProxyManager().destroyProxy(handle);
            }
        );
    }

    template <> Type* Reflection::createType<StaticMesh::DistanceFieldData>()
    {
        auto s = new TStructure<StaticMesh::DistanceFieldData>("StaticMesh.DistanceFieldData");
        s->addProperty("Resolution", &StaticMesh::DistanceFieldData::resolution);
        s->addProperty("Bounds", &StaticMesh::DistanceFieldData::bounds);
        s->addProperty("NarrowBandDistance", &StaticMesh::DistanceFieldData::narrowBandDistance);
        s->addProperty("BrickGridResolution", &StaticMesh::DistanceFieldData::brickGridResolution);
        s->addProperty("BrickIndirection", &StaticMesh::DistanceFieldData::brickIndirection);
        s->addProperty("BrickData", &StaticMesh::DistanceFieldData::brickData);
        return s;
    }

    template <> Type* Reflection::createType<StaticMesh::CardData>()
    {
        auto s = new TStructure<StaticMesh::CardData>("StaticMesh.CardData");
        s->addProperty("MeshCardsBounds", &StaticMesh::CardData::meshCardsBounds);
        s->addProperty("MostlyTwoSided", &StaticMesh::CardData::mostlyTwoSided);
        s->addProperty("Cards", &StaticMesh::CardData::cards);
        return s;
    }

    template <> Type* Reflection::createType<StaticMesh>()
    {
        auto c = new TClass<StaticMesh, Mesh>("StaticMesh");
        c->setDisplayName("Static Mesh");
        c->addProperty("DistanceFieldResolutionScale", &StaticMesh::mDistanceFieldResolutionScale);
        c->addProperty("DistanceFieldData", &StaticMesh::mDistanceFieldData, CopyPolicy::Shallow, PropertyFlags::Cook_Only);
        c->addProperty("CardData", &StaticMesh::mCardData);
        return c;
    }
}
